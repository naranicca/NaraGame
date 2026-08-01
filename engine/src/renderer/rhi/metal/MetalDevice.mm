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
        : m_size(desc.size) {
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
        auto* dst = static_cast<unsigned char*>(
            [(__bridge id<MTLBuffer>)m_buffer contents]);
        std::memcpy(dst + offset, data, count);
    }
    size_t Size() const override { return m_size; }
    id<MTLBuffer> Native() const { return (__bridge id<MTLBuffer>)m_buffer; }
private:
    void* m_buffer = nullptr;
    size_t m_size = 0;
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
          m_format(desc.format), m_layers(std::max(1u, desc.arrayLayers)) {
        if (m_width == 0 || m_height == 0) return;
        MTLTextureDescriptor* td = [MTLTextureDescriptor new];
        td.pixelFormat = ToMetalFormat(desc.format);
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
private:
    void* m_texture = nullptr;
    uint32_t m_width = 0, m_height = 0, m_layers = 1;
    TextureFilter m_filter = TextureFilter::Linear;
    TextureFormat m_format = TextureFormat::RGBA8;
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
    if (drawable == nil || commands == nil) return;
    if (m_encoder != nullptr) {
        [(__bridge id<MTLRenderCommandEncoder>)m_encoder endEncoding];
        (void)(__bridge_transfer id<MTLRenderCommandEncoder>)m_encoder;
        m_encoder = nullptr;
    }
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor =
        MTLClearColorMake(color.r, color.g, color.b, color.a);
    id<MTLRenderCommandEncoder> encoder =
        [commands renderCommandEncoderWithDescriptor:pass];
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
std::unique_ptr<RHIShader> MetalDevice::CreateShader(const ShaderDesc&) {
    NARAGAME_WARN("Metal shaders are not implemented yet");
    return nullptr;
}
std::unique_ptr<RHIPipeline> MetalDevice::CreatePipeline(const PipelineDesc&) {
    NARAGAME_WARN("Metal pipelines are not implemented yet");
    return nullptr;
}

} // namespace NaraGame

#endif
