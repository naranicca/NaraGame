#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_HAVE_VULKAN)

#include "NaraGame/renderer/rhi/RHIResources.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace NaraGame {

// Shared handles the resource objects need from the device.
struct VulkanContext {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool uploadPool = VK_NULL_HANDLE; // one-time transfer commands
    VkRenderPass renderPass = VK_NULL_HANDLE;
    // Render pass used by render-target textures (load DONT_CARE, finalLayout
    // SHADER_READ_ONLY_OPTIMAL) so each RT VkImage ends a draw ready to be
    // sampled back as a sprite.
    VkRenderPass offscreenRenderPass = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDeviceSize minUniformAlignment = 256;
    const uint64_t* frame = nullptr; // device frame counter, for per-frame ring reset
};

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits,
                        VkMemoryPropertyFlags properties);

// Host-visible buffer. Dynamic buffers ring-allocate per frame so multiple
// updates within one frame land in distinct regions (Vulkan executes the
// command buffer later, unlike D3D11's immediate context).
class VulkanBuffer final : public RHIBuffer {
public:
    VulkanBuffer(const VulkanContext& ctx, const BufferDesc& desc);
    ~VulkanBuffer() override;

    void Update(const void* data, size_t size, size_t offset) override;
    size_t Size() const override { return static_cast<size_t>(m_capacity); }

    VkBuffer Native() const { return m_buffer; }
    VkDeviceSize CurrentOffset() const { return m_currentOffset; }

private:
    const VulkanContext* m_ctx;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    void* m_mapped = nullptr;
    VkDeviceSize m_capacity = 0;
    VkDeviceSize m_cursor = 0;
    VkDeviceSize m_currentOffset = 0;
    VkDeviceSize m_align = 4;
    bool m_dynamic = false;
    uint64_t m_lastFrame = ~0ull;
};

class VulkanTexture final : public RHITexture {
public:
    VulkanTexture(const VulkanContext& ctx, const TextureDesc& desc);
    ~VulkanTexture() override;

    uint32_t Width() const override { return m_width; }
    uint32_t Height() const override { return m_height; }
    bool UpdatePixels(const void* pixels) override;
    VkImageView View() const { return m_view; }
    // Framebuffer keyed to the offscreen render pass. Non-null only when the
    // texture was created with TextureDesc::renderTarget = true.
    VkFramebuffer Framebuffer() const { return m_framebuffer; }
    bool IsRenderTarget() const { return m_framebuffer != VK_NULL_HANDLE; }

private:
    const VulkanContext* m_ctx;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;  // RT-only
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

class VulkanShader final : public RHIShader {
public:
    VulkanShader(const VulkanContext& ctx, const ShaderDesc& desc);
    ~VulkanShader() override;

    bool Valid() const override { return m_vert && m_frag; }
    VkShaderModule Vertex() const { return m_vert; }
    VkShaderModule Fragment() const { return m_frag; }

private:
    const VulkanContext* m_ctx;
    VkShaderModule m_vert = VK_NULL_HANDLE;
    VkShaderModule m_frag = VK_NULL_HANDLE;
};

class VulkanPipeline final : public RHIPipeline {
public:
    VulkanPipeline(const VulkanContext& ctx, const PipelineDesc& desc);
    ~VulkanPipeline() override;

    VkPipeline Native() const { return m_pipeline; }
    VkPipelineLayout Layout() const { return m_layout; }

private:
    const VulkanContext* m_ctx;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace NaraGame

#endif // NARAGAME_HAVE_VULKAN
