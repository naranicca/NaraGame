#include "renderer/rhi/metal/MetalDevice.h"

#if defined(NARAGAME_PLATFORM_MACOS) || defined(NARAGAME_PLATFORM_IOS)

#include "NaraGame/core/Log.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cstring>

namespace NaraGame {

namespace {

class MetalBuffer final : public RHIBuffer {
public:
    MetalBuffer(id<MTLDevice> device, const BufferDesc& desc)
        : m_device((__bridge void*)device), m_size(desc.size),
          m_dynamic(desc.usage == BufferUsage::Dynamic) {
        if (m_size == 0) return;
        MTLResourceOptions options = MTLResourceStorageModeShared;
        id<MTLBuffer> buffer = desc.initialData
            ? [device newBufferWithBytes:desc.initialData
                                  length:m_size options:options]
            : [device newBufferWithLength:m_size options:options];
        m_buffer = (__bridge_retained void*)buffer;
    }
    ~MetalBuffer() override {
        (void)(__bridge_transfer id<MTLBuffer>)m_buffer;
    }
    void Update(const void* data, size_t size, size_t offset) override {
        if (data == nullptr || m_buffer == nullptr || offset >= m_size) return;
        const size_t count = std::min(size, m_size - offset);
        // A shared buffer may still be referenced by draws already encoded in
        // this command buffer. Dynamic updates therefore use discard semantics:
        // replace the allocation and let Metal retain the old resource until
        // those commands complete.
        if (m_dynamic && offset == 0) {
            id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
            id<MTLBuffer> replacement = [device newBufferWithLength:m_size
                                                            options:MTLResourceStorageModeShared];
            if (replacement == nil) return;
            void* old = m_buffer;
            m_buffer = (__bridge_retained void*)replacement;
            (void)(__bridge_transfer id<MTLBuffer>)old;
        }
        auto* dst = static_cast<unsigned char*>(
            [(__bridge id<MTLBuffer>)m_buffer contents]);
        std::memcpy(dst + offset, data, count);
    }
    size_t Size() const override { return m_size; }
    id<MTLBuffer> Native() const { return (__bridge id<MTLBuffer>)m_buffer; }
private:
    void* m_device = nullptr; // unretained; RHIDevice outlives its resources
    void* m_buffer = nullptr;
    size_t m_size = 0;
    bool m_dynamic = false;
};

static MTLPixelFormat ToMetalFormat(TextureFormat format) {
    switch (format) {
    case TextureFormat::RGBA16F: return MTLPixelFormatRGBA16Float;
    case TextureFormat::Depth24: return MTLPixelFormatDepth32Float;
    default:                     return MTLPixelFormatRGBA8Unorm;
    }
}

class MetalTexture final : public RHITexture {
public:
    MetalTexture(id<MTLDevice> device, const TextureDesc& desc)
        : m_width(desc.width), m_height(desc.height), m_filter(desc.filter),
          m_format(desc.format), m_layers(std::max(1u, desc.arrayLayers)),
          m_renderTarget(desc.renderTarget) {
        if (m_width == 0 || m_height == 0) return;
        MTLTextureDescriptor* td = [MTLTextureDescriptor new];
        td.pixelFormat = desc.renderTarget && desc.format == TextureFormat::RGBA8
            ? MTLPixelFormatBGRA8Unorm : ToMetalFormat(desc.format);
        td.width = m_width;
        td.height = m_height;
        td.mipmapLevelCount = 1;
        td.storageMode = MTLStorageModeShared;
        td.textureType = desc.isCube ? MTLTextureTypeCube
            : (m_layers > 1 ? MTLTextureType2DArray : MTLTextureType2D);
        td.arrayLength = desc.isCube ? 1 : m_layers;
        td.usage = MTLTextureUsageShaderRead |
            (desc.renderTarget ? MTLTextureUsageRenderTarget : 0);
        id<MTLTexture> texture = [device newTextureWithDescriptor:td];
        m_texture = (__bridge_retained void*)texture;
        if (desc.pixels != nullptr && !desc.renderTarget) UpdatePixels(desc.pixels);
    }
    ~MetalTexture() override {
        (void)(__bridge_transfer id<MTLTexture>)m_texture;
    }
    uint32_t Width() const override { return m_width; }
    uint32_t Height() const override { return m_height; }
    TextureFilter Filter() const override { return m_filter; }
    bool UpdatePixels(const void* pixels) override {
        id<MTLTexture> texture = (__bridge id<MTLTexture>)m_texture;
        if (texture == nil || pixels == nullptr || m_format == TextureFormat::Depth24)
            return false;
        const size_t bytesPerPixel = m_format == TextureFormat::RGBA16F ? 8 : 4;
        const size_t bytesPerRow = size_t(m_width) * bytesPerPixel;
        const size_t bytesPerLayer = bytesPerRow * m_height;
        const uint32_t slices = texture.textureType == MTLTextureTypeCube ? 6 : m_layers;
        for (uint32_t slice = 0; slice < slices; ++slice) {
            MTLRegion region = MTLRegionMake2D(0, 0, m_width, m_height);
            [texture replaceRegion:region mipmapLevel:0 slice:slice
                         withBytes:static_cast<const unsigned char*>(pixels) +
                                   bytesPerLayer * slice
                       bytesPerRow:bytesPerRow bytesPerImage:bytesPerLayer];
        }
        return true;
    }
    void* GetNativeView() const override { return m_texture; }
    id<MTLTexture> Native() const { return (__bridge id<MTLTexture>)m_texture; }
    bool IsRenderTarget() const { return m_renderTarget; }
private:
    void* m_texture = nullptr;
    uint32_t m_width = 0, m_height = 0, m_layers = 1;
    TextureFilter m_filter = TextureFilter::Linear;
    TextureFormat m_format = TextureFormat::RGBA8;
    bool m_renderTarget = false;
};

class MetalShader final : public RHIShader {
public:
    MetalShader(id<MTLDevice> device, const ShaderDesc& desc) {
        if (desc.metalSource.empty()) return;
        NSError* error = nil;
        NSString* source = [NSString stringWithUTF8String:desc.metalSource.c_str()];
        id<MTLLibrary> library = [device newLibraryWithSource:source
                                                     options:nil error:&error];
        if (library == nil) {
            NARAGAME_ERROR("Metal shader '%s' failed: %s", desc.debugName.c_str(),
                error.localizedDescription.UTF8String);
            return;
        }
        id<MTLFunction> vertex = [library newFunctionWithName:
            [NSString stringWithUTF8String:desc.metalVertexEntry.c_str()]];
        id<MTLFunction> fragment = [library newFunctionWithName:
            [NSString stringWithUTF8String:desc.metalPixelEntry.c_str()]];
        m_library = (__bridge_retained void*)library;
        m_vertex = (__bridge_retained void*)vertex;
        m_fragment = (__bridge_retained void*)fragment;
    }
    ~MetalShader() override {
        (void)(__bridge_transfer id<MTLFunction>)m_fragment;
        (void)(__bridge_transfer id<MTLFunction>)m_vertex;
        (void)(__bridge_transfer id<MTLLibrary>)m_library;
    }
    bool Valid() const override { return m_vertex && m_fragment; }
    id<MTLFunction> Vertex() const { return (__bridge id<MTLFunction>)m_vertex; }
    id<MTLFunction> Fragment() const { return (__bridge id<MTLFunction>)m_fragment; }
private:
    void* m_library = nullptr;
    void* m_vertex = nullptr;
    void* m_fragment = nullptr;
};

class MetalPipeline final : public RHIPipeline {
public:
    MetalPipeline(id<MTLDevice> device, const PipelineDesc& desc) {
        auto* shader = dynamic_cast<MetalShader*>(desc.shader);
        if (shader == nullptr || !shader->Valid()) return;
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = shader->Vertex();
        pd.fragmentFunction = shader->Fragment();
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        auto* color = pd.colorAttachments[0];
        color.blendingEnabled = desc.blend != BlendMode::None;
        if (desc.blend == BlendMode::Alpha) {
            color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        } else if (desc.blend == BlendMode::Additive) {
            color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            color.destinationRGBBlendFactor = MTLBlendFactorOne;
        } else if (desc.blend == BlendMode::Multiply) {
            color.sourceRGBBlendFactor = MTLBlendFactorDestinationColor;
            color.destinationRGBBlendFactor = MTLBlendFactorZero;
        }
        color.sourceAlphaBlendFactor = MTLBlendFactorOne;
        color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        for (NSUInteger i = 0; i < desc.vertexLayout.attributes.size(); ++i) {
            const auto& attribute = desc.vertexLayout.attributes[i];
            MTLVertexFormat format = MTLVertexFormatFloat;
            switch (attribute.format) {
            case VertexFormat::Float2: format = MTLVertexFormatFloat2; break;
            case VertexFormat::Float3: format = MTLVertexFormatFloat3; break;
            case VertexFormat::Float4: format = MTLVertexFormatFloat4; break;
            case VertexFormat::UByte4Norm: format = MTLVertexFormatUChar4Normalized; break;
            default: break;
            }
            vd.attributes[i].format = format;
            vd.attributes[i].offset = attribute.offset;
            vd.attributes[i].bufferIndex = 0;
        }
        vd.layouts[0].stride = desc.vertexLayout.stride;
        vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        pd.vertexDescriptor = vd;
        NSError* error = nil;
        id<MTLRenderPipelineState> pipeline =
            [device newRenderPipelineStateWithDescriptor:pd error:&error];
        if (pipeline == nil) {
            NARAGAME_ERROR("Metal pipeline failed: %s",
                           error.localizedDescription.UTF8String);
            return;
        }
        m_pipeline = (__bridge_retained void*)pipeline;
    }
    ~MetalPipeline() override {
        (void)(__bridge_transfer id<MTLRenderPipelineState>)m_pipeline;
    }
    id<MTLRenderPipelineState> Native() const {
        return (__bridge id<MTLRenderPipelineState>)m_pipeline;
    }
    bool Valid() const { return m_pipeline != nullptr; }
private:
    void* m_pipeline = nullptr;
};

} // namespace

MetalDevice::MetalDevice(const RHIDeviceDesc& desc)
    : m_layer(desc.nativeWindowHandle), m_width(desc.width),
      m_height(desc.height) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        CAMetalLayer* layer = (__bridge CAMetalLayer*)m_layer;
        if (device == nil || layer == nil) {
            NARAGAME_ERROR("Metal: device or CAMetalLayer is unavailable");
            return;
        }
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.drawableSize = CGSizeMake(desc.width, desc.height);
        id<MTLCommandQueue> queue = [device newCommandQueue];
        MTLSamplerDescriptor* linearDesc = [MTLSamplerDescriptor new];
        linearDesc.minFilter = MTLSamplerMinMagFilterLinear;
        linearDesc.magFilter = MTLSamplerMinMagFilterLinear;
        MTLSamplerDescriptor* pointDesc = [MTLSamplerDescriptor new];
        pointDesc.minFilter = MTLSamplerMinMagFilterNearest;
        pointDesc.magFilter = MTLSamplerMinMagFilterNearest;
        m_linearSampler = (__bridge_retained void*)[device newSamplerStateWithDescriptor:linearDesc];
        m_pointSampler = (__bridge_retained void*)[device newSamplerStateWithDescriptor:pointDesc];
        m_device = (__bridge_retained void*)device;
        m_queue = (__bridge_retained void*)queue;
    }
}

MetalDevice::~MetalDevice() {
    @autoreleasepool {
        (void)(__bridge_transfer id<MTLRenderCommandEncoder>)m_encoder;
        (void)(__bridge_transfer id<MTLCommandBuffer>)m_commandBuffer;
        (void)(__bridge_transfer id<CAMetalDrawable>)m_drawable;
        (void)(__bridge_transfer id<MTLCommandQueue>)m_queue;
        (void)(__bridge_transfer id<MTLSamplerState>)m_pointSampler;
        (void)(__bridge_transfer id<MTLSamplerState>)m_linearSampler;
        (void)(__bridge_transfer id<MTLDevice>)m_device;
    }
}

void MetalDevice::Resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)m_layer;
    layer.drawableSize = CGSizeMake(width, height);
}

void MetalDevice::BeginFrame() {
    @autoreleasepool {
        m_renderTarget = nullptr;
        CAMetalLayer* layer = (__bridge CAMetalLayer*)m_layer;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)m_queue;
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        m_drawable = (__bridge_retained void*)drawable;
        m_commandBuffer = (__bridge_retained void*)commands;
    }
}

void MetalDevice::Clear(const Color& color) {
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)m_drawable;
    id<MTLCommandBuffer> commands =
        (__bridge id<MTLCommandBuffer>)m_commandBuffer;
    auto* target = dynamic_cast<MetalTexture*>(m_renderTarget);
    id<MTLTexture> colorTexture = target ? target->Native()
                                         : (drawable ? drawable.texture : nil);
    if (colorTexture == nil || commands == nil) return;
    if (m_encoder != nullptr) {
        [(__bridge id<MTLRenderCommandEncoder>)m_encoder endEncoding];
        (void)(__bridge_transfer id<MTLRenderCommandEncoder>)m_encoder;
        m_encoder = nullptr;
    }
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = colorTexture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor =
        MTLClearColorMake(color.r, color.g, color.b, color.a);
    id<MTLRenderCommandEncoder> encoder =
        [commands renderCommandEncoderWithDescriptor:pass];
    const double width = target ? target->Width() : m_width;
    const double height = target ? target->Height() : m_height;
    [encoder setViewport:MTLViewport{0.0, 0.0, width, height, 0.0, 1.0}];
    m_encoder = (__bridge_retained void*)encoder;
}

void MetalDevice::SetRenderTarget(RHITexture* renderTarget) {
    if (renderTarget != nullptr) {
        auto* texture = dynamic_cast<MetalTexture*>(renderTarget);
        if (texture == nullptr || !texture->IsRenderTarget()) return;
    }
    if (m_encoder != nullptr) {
        [(__bridge id<MTLRenderCommandEncoder>)m_encoder endEncoding];
        (void)(__bridge_transfer id<MTLRenderCommandEncoder>)m_encoder;
        m_encoder = nullptr;
    }
    m_renderTarget = renderTarget;
    id<MTLCommandBuffer> commands =
        (__bridge id<MTLCommandBuffer>)m_commandBuffer;
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)m_drawable;
    auto* target = dynamic_cast<MetalTexture*>(m_renderTarget);
    id<MTLTexture> colorTexture = target ? target->Native()
                                         : (drawable ? drawable.texture : nil);
    if (commands == nil || colorTexture == nil) return;
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = colorTexture;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> encoder =
        [commands renderCommandEncoderWithDescriptor:pass];
    const double width = target ? target->Width() : m_width;
    const double height = target ? target->Height() : m_height;
    [encoder setViewport:MTLViewport{0.0, 0.0, width, height, 0.0, 1.0}];
    m_encoder = (__bridge_retained void*)encoder;
}

void MetalDevice::EndFrame() {
    if (m_encoder != nullptr) {
        [(__bridge id<MTLRenderCommandEncoder>)m_encoder endEncoding];
        (void)(__bridge_transfer id<MTLRenderCommandEncoder>)m_encoder;
        m_encoder = nullptr;
    }
}

void MetalDevice::Present() {
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)m_drawable;
    id<MTLCommandBuffer> commands =
        (__bridge id<MTLCommandBuffer>)m_commandBuffer;
    if (drawable != nil && commands != nil) {
        [commands presentDrawable:drawable];
        [commands commit];
    }
    (void)(__bridge_transfer id<MTLCommandBuffer>)m_commandBuffer;
    (void)(__bridge_transfer id<CAMetalDrawable>)m_drawable;
    m_commandBuffer = nullptr;
    m_drawable = nullptr;
}

std::unique_ptr<RHIBuffer> MetalDevice::CreateBuffer(const BufferDesc& desc) {
    auto resource = std::make_unique<MetalBuffer>(
        (__bridge id<MTLDevice>)m_device, desc);
    return resource->Size() > 0 ? std::move(resource) : nullptr;
}
std::unique_ptr<RHITexture> MetalDevice::CreateTexture(const TextureDesc& desc) {
    auto resource = std::make_unique<MetalTexture>(
        (__bridge id<MTLDevice>)m_device, desc);
    return resource->GetNativeView() != nullptr ? std::move(resource) : nullptr;
}
std::unique_ptr<RHIShader> MetalDevice::CreateShader(const ShaderDesc& desc) {
    auto shader = std::make_unique<MetalShader>(
        (__bridge id<MTLDevice>)m_device, desc);
    return shader->Valid() ? std::move(shader) : nullptr;
}
std::unique_ptr<RHIPipeline> MetalDevice::CreatePipeline(const PipelineDesc& desc) {
    auto pipeline = std::make_unique<MetalPipeline>(
        (__bridge id<MTLDevice>)m_device, desc);
    return pipeline->Valid() ? std::move(pipeline) : nullptr;
}

void MetalDevice::BindPipeline(RHIPipeline* pipeline) {
    auto* metal = dynamic_cast<MetalPipeline*>(pipeline);
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)m_encoder;
    if (metal && metal->Valid() && encoder)
        [encoder setRenderPipelineState:metal->Native()];
}
void MetalDevice::BindVertexBuffer(RHIBuffer* buffer, uint32_t, uint32_t offset) {
    auto* metal = dynamic_cast<MetalBuffer*>(buffer);
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)m_encoder;
    if (metal && encoder) [encoder setVertexBuffer:metal->Native() offset:offset atIndex:0];
}
void MetalDevice::BindIndexBuffer(RHIBuffer* buffer, IndexFormat format) {
    m_indexBuffer = buffer; m_indexFormat = format;
}
void MetalDevice::BindUniformBuffer(uint32_t slot, RHIBuffer* buffer) {
    auto* metal = dynamic_cast<MetalBuffer*>(buffer);
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)m_encoder;
    if (metal && encoder) [encoder setVertexBuffer:metal->Native() offset:0 atIndex:slot + 1];
}
void MetalDevice::BindTexture(uint32_t slot, RHITexture* texture) {
    auto* metal = dynamic_cast<MetalTexture*>(texture);
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)m_encoder;
    if (!metal || !encoder) return;
    [encoder setFragmentTexture:metal->Native() atIndex:slot];
    id<MTLSamplerState> sampler = (__bridge id<MTLSamplerState>)(
        metal->Filter() == TextureFilter::Point ? m_pointSampler : m_linearSampler);
    [encoder setFragmentSamplerState:sampler atIndex:slot];
}
void MetalDevice::DrawIndexed(uint32_t indexCount, uint32_t firstIndex) {
    auto* index = dynamic_cast<MetalBuffer*>(m_indexBuffer);
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)m_encoder;
    if (!index || !encoder || indexCount == 0) return;
    const MTLIndexType type = m_indexFormat == IndexFormat::Uint16
        ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
    const uint32_t stride = m_indexFormat == IndexFormat::Uint16 ? 2u : 4u;
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:indexCount indexType:type
                       indexBuffer:index->Native()
                 indexBufferOffset:firstIndex * stride];
}

} // namespace NaraGame

#endif
