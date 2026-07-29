#include "renderer/rhi/metal/MetalDevice.h"

#if defined(NARAGAME_PLATFORM_MACOS) || defined(NARAGAME_PLATFORM_IOS)

#include "NaraGame/core/Log.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace NaraGame {

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

std::unique_ptr<RHIBuffer> MetalDevice::CreateBuffer(const BufferDesc&) {
    NARAGAME_WARN("Metal buffer resources are not implemented yet");
    return nullptr;
}
std::unique_ptr<RHITexture> MetalDevice::CreateTexture(const TextureDesc&) {
    NARAGAME_WARN("Metal texture resources are not implemented yet");
    return nullptr;
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
