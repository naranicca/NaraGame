#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_HAVE_VULKAN)

#include "NaraGame/renderer/rhi/RHIDevice.h"
#include "renderer/rhi/vulkan/VulkanResources.h"

#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

namespace NaraGame {

// Vulkan RHI backend. Stage 1 covers full device/swapchain bring-up plus clear
// and present; resource/draw entry points are filled in stage 2.
class VulkanDevice final : public RHIDevice {
public:
    explicit VulkanDevice(const RHIDeviceDesc& desc);
    ~VulkanDevice() override;

    bool Valid() const { return m_device != VK_NULL_HANDLE && m_swapchain != VK_NULL_HANDLE; }

    void Resize(uint32_t width, uint32_t height) override;
    void BeginFrame() override;
    void Clear(const Color& color) override;
    void EndFrame() override;
    void Present() override;
    void SetRenderTarget(RHITexture* target) override;

    std::unique_ptr<RHIBuffer> CreateBuffer(const BufferDesc& desc) override;
    std::unique_ptr<RHITexture> CreateTexture(const TextureDesc& desc) override;
    std::unique_ptr<RHIShader> CreateShader(const ShaderDesc& desc) override;
    std::unique_ptr<RHIPipeline> CreatePipeline(const PipelineDesc& desc) override;

    void BindPipeline(RHIPipeline* pipeline) override;
    void BindVertexBuffer(RHIBuffer* buffer, uint32_t stride, uint32_t offset) override;
    void BindIndexBuffer(RHIBuffer* buffer, IndexFormat format) override;
    void BindUniformBuffer(uint32_t slot, RHIBuffer* buffer) override;
    void BindTexture(uint32_t slot, RHITexture* texture) override;
    void DrawIndexed(uint32_t indexCount, uint32_t firstIndex) override;

    RHIBackend Backend() const override { return RHIBackend::Vulkan; }
    const char* BackendName() const override { return "Vulkan"; }

private:
    bool CreateInstance();
    bool CreateDeviceAndQueue();
    bool CreateSwapchain(uint32_t width, uint32_t height);
    void DestroySwapchain();
    bool CreateSyncAndCommands();
    void CreatePipelineResources(); // sampler, descriptor layout/pool, context
    VkDescriptorSet GetDescriptorSet();

    // Combined cache key for descriptor sets. Two textures (sprite/fog) plus
    // the fog UBO buffer disambiguate everything we ever bind: the camera UBO
    // is a single device-wide buffer so it doesn't need to be part of the key.
    struct DescKey {
        VkImageView spriteView = VK_NULL_HANDLE;
        VkImageView fogView    = VK_NULL_HANDLE;
        VkBuffer    fogUbo     = VK_NULL_HANDLE;
        bool operator==(const DescKey& o) const noexcept {
            return spriteView == o.spriteView && fogView == o.fogView && fogUbo == o.fogUbo;
        }
    };
    struct DescKeyHash {
        size_t operator()(const DescKey& k) const noexcept {
            uint64_t a = 0, b = 0, c = 0;
            std::memcpy(&a, &k.spriteView, sizeof(a));
            std::memcpy(&b, &k.fogView,    sizeof(b));
            std::memcpy(&c, &k.fogUbo,     sizeof(c));
            const std::hash<uint64_t> h;
            return h(a) ^ (h(b) * 0x9e3779b97f4a7c15ull) ^ (h(c) * 0xc2b2ae3d27d4eb4full);
        }
    };

    HWND m_hwnd = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_vsync = true;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VkQueue m_queue = VK_NULL_HANDLE;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D m_swapExtent{};
    std::vector<VkImage> m_swapImages;
    std::vector<VkImageView> m_swapViews;
    std::vector<VkFramebuffer> m_framebuffers;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkSemaphore m_imageAvailable = VK_NULL_HANDLE;
    std::vector<VkSemaphore> m_renderFinished; // one per swapchain image
    VkFence m_inFlight = VK_NULL_HANDLE;

    uint32_t m_imageIndex = 0;
    bool m_frameValid = false; // false if acquire failed (skip the frame)
    bool m_passActive = false;

    // Stage 2: resources, descriptors, per-draw state.
    uint64_t m_frame = 0;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VulkanContext m_ctx;
    std::unordered_map<DescKey, VkDescriptorSet, DescKeyHash> m_descriptorCache;

    VkPipelineLayout m_boundLayout = VK_NULL_HANDLE;
    // Slot 0 (binding 0 / binding 1): camera UBO + sprite texture.
    VulkanBuffer*  m_boundUbo        = nullptr;
    VkDeviceSize   m_boundUboOffset  = 0;
    VulkanTexture* m_boundTexture    = nullptr;
    // Slot 1 (binding 3 / binding 2): fog xform UBO + fog texture.
    // Null when the active pipeline doesn't read fog -- GetDescriptorSet()
    // substitutes the slot-0 resources so the descriptor set is fully valid.
    VulkanBuffer*  m_boundFogUbo       = nullptr;
    VkDeviceSize   m_boundFogUboOffset = 0;
    VulkanTexture* m_boundFogTexture   = nullptr;
};

} // namespace NaraGame

#endif // NARAGAME_HAVE_VULKAN
