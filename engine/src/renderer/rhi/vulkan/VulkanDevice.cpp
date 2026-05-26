#include "renderer/rhi/vulkan/VulkanDevice.h"

#if defined(NARAGAME_HAVE_VULKAN)

#include "NaraGame/core/Log.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace NaraGame {
namespace {

#if !defined(NDEBUG)
constexpr bool kEnableValidation = true;
#else
constexpr bool kEnableValidation = false;
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void*) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        NARAGAME_ERROR("[vk] %s", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        NARAGAME_WARN("[vk] %s", data->pMessage);
    return VK_FALSE;
}

bool HasLayer(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers)
        if (std::strcmp(l.layerName, name) == 0)
            return true;
    return false;
}

} // namespace

VulkanDevice::VulkanDevice(const RHIDeviceDesc& desc)
    : m_hwnd(static_cast<HWND>(desc.nativeWindowHandle))
    , m_width(desc.width)
    , m_height(desc.height)
    , m_vsync(desc.vsync) {
    if (!CreateInstance())
        return;

    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    sci.hinstance = GetModuleHandleW(nullptr);
    sci.hwnd = m_hwnd;
    if (vkCreateWin32SurfaceKHR(m_instance, &sci, nullptr, &m_surface) != VK_SUCCESS) {
        NARAGAME_ERROR("vkCreateWin32SurfaceKHR failed");
        return;
    }

    if (!CreateDeviceAndQueue())
        return;
    if (!CreateSwapchain(m_width, m_height))
        return;

    // Render pass: single color attachment, clear -> store -> present.
    VkAttachmentDescription color{};
    color.format = m_swapFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    if (vkCreateRenderPass(m_device, &rp, nullptr, &m_renderPass) != VK_SUCCESS) {
        NARAGAME_ERROR("vkCreateRenderPass failed");
        return;
    }

    // Framebuffers (one per swapchain image).
    m_framebuffers.resize(m_swapViews.size());
    for (size_t i = 0; i < m_swapViews.size(); ++i) {
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = m_renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &m_swapViews[i];
        fb.width = m_swapExtent.width;
        fb.height = m_swapExtent.height;
        fb.layers = 1;
        vkCreateFramebuffer(m_device, &fb, nullptr, &m_framebuffers[i]);
    }

    if (!CreateSyncAndCommands())
        return;
    CreatePipelineResources();

    NARAGAME_INFO("Vulkan device created (%ux%u, %zu images)", m_swapExtent.width, m_swapExtent.height,
              m_swapImages.size());
}

VulkanDevice::~VulkanDevice() {
    if (m_device)
        vkDeviceWaitIdle(m_device);

    if (m_descriptorPool)
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_descriptorLayout)
        vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr);
    if (m_sampler)
        vkDestroySampler(m_device, m_sampler, nullptr);

    if (m_inFlight)
        vkDestroyFence(m_device, m_inFlight, nullptr);
    if (m_imageAvailable)
        vkDestroySemaphore(m_device, m_imageAvailable, nullptr);
    if (m_commandPool)
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);

    DestroySwapchain();
    if (m_renderPass)
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    if (m_device)
        vkDestroyDevice(m_device, nullptr);

    if (m_debugMessenger) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy)
            destroy(m_instance, m_debugMessenger, nullptr);
    }
    if (m_surface)
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance)
        vkDestroyInstance(m_instance, nullptr);
}

bool VulkanDevice::CreateInstance() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "NaraGame";
    app.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> extensions = {VK_KHR_SURFACE_EXTENSION_NAME,
                                           VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    std::vector<const char*> layers;
    const bool validation = kEnableValidation && HasLayer("VK_LAYER_KHRONOS_validation");
    if (validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS) {
        NARAGAME_ERROR("vkCreateInstance failed");
        return false;
    }

    if (validation) {
        VkDebugUtilsMessengerCreateInfoEXT dci{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dci.pfnUserCallback = DebugCallback;
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create)
            create(m_instance, &dci, nullptr, &m_debugMessenger);
    }
    return true;
}

bool VulkanDevice::CreateDeviceAndQueue() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        NARAGAME_ERROR("No Vulkan physical devices");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    for (VkPhysicalDevice dev : devices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &familyCount, families.data());
        for (uint32_t i = 0; i < familyCount; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_surface, &present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                m_physicalDevice = dev;
                m_queueFamily = i;
                break;
            }
        }
        if (m_physicalDevice != VK_NULL_HANDLE)
            break;
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
        NARAGAME_ERROR("No suitable Vulkan queue family");
        return false;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = m_queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    const char* deviceExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = deviceExt;
    if (vkCreateDevice(m_physicalDevice, &dci, nullptr, &m_device) != VK_SUCCESS) {
        NARAGAME_ERROR("vkCreateDevice failed");
        return false;
    }
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    NARAGAME_INFO("Vulkan GPU: %s", props.deviceName);
    return true;
}

bool VulkanDevice::CreateSwapchain(uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    m_swapFormat = chosen.format;

    if (caps.currentExtent.width != UINT32_MAX) {
        m_swapExtent = caps.currentExtent;
    } else {
        m_swapExtent.width =
            std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        m_swapExtent.height =
            std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (m_swapExtent.width == 0 || m_swapExtent.height == 0)
        return false;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sc.surface = m_surface;
    sc.minImageCount = imageCount;
    sc.imageFormat = chosen.format;
    sc.imageColorSpace = chosen.colorSpace;
    sc.imageExtent = m_swapExtent;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sc.presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync, always supported
    sc.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(m_device, &sc, nullptr, &m_swapchain) != VK_SUCCESS) {
        NARAGAME_ERROR("vkCreateSwapchainKHR failed");
        return false;
    }

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr);
    m_swapImages.resize(count);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, m_swapImages.data());

    m_swapViews.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo iv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        iv.image = m_swapImages[i];
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = m_swapFormat;
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.layerCount = 1;
        vkCreateImageView(m_device, &iv, nullptr, &m_swapViews[i]);
    }

    // One present (render-finished) semaphore per image to avoid unsafe reuse.
    m_renderFinished.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(m_device, &sem, nullptr, &m_renderFinished[i]);
    }
    return true;
}

void VulkanDevice::DestroySwapchain() {
    for (VkSemaphore s : m_renderFinished)
        vkDestroySemaphore(m_device, s, nullptr);
    m_renderFinished.clear();
    for (VkFramebuffer fb : m_framebuffers)
        vkDestroyFramebuffer(m_device, fb, nullptr);
    m_framebuffers.clear();
    for (VkImageView v : m_swapViews)
        vkDestroyImageView(m_device, v, nullptr);
    m_swapViews.clear();
    m_swapImages.clear();
    if (m_swapchain) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool VulkanDevice::CreateSyncAndCommands() {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = m_queueFamily;
    vkCreateCommandPool(m_device, &pool, nullptr, &m_commandPool);

    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = m_commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_device, &alloc, &m_commandBuffer);

    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(m_device, &sem, nullptr, &m_imageAvailable);
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(m_device, &fence, nullptr, &m_inFlight);
    return true;
}

void VulkanDevice::Resize(uint32_t width, uint32_t height) {
    if (!Valid() || width == 0 || height == 0)
        return;
    m_width = width;
    m_height = height;
    vkDeviceWaitIdle(m_device);
    DestroySwapchain();
    CreateSwapchain(m_width, m_height);
    m_framebuffers.resize(m_swapViews.size());
    for (size_t i = 0; i < m_swapViews.size(); ++i) {
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = m_renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &m_swapViews[i];
        fb.width = m_swapExtent.width;
        fb.height = m_swapExtent.height;
        fb.layers = 1;
        vkCreateFramebuffer(m_device, &fb, nullptr, &m_framebuffers[i]);
    }
}

void VulkanDevice::CreatePipelineResources() {
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    vkCreateSampler(m_device, &sampler, nullptr, &m_sampler);

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2;
    dl.pBindings = bindings;
    vkCreateDescriptorSetLayout(m_device, &dl, nullptr, &m_descriptorLayout);

    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    sizes[0].descriptorCount = 128;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = 128;
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = 128;
    dp.poolSizeCount = 2;
    dp.pPoolSizes = sizes;
    vkCreateDescriptorPool(m_device, &dp, nullptr, &m_descriptorPool);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);

    m_ctx.physicalDevice = m_physicalDevice;
    m_ctx.device = m_device;
    m_ctx.queue = m_queue;
    m_ctx.uploadPool = m_commandPool;
    m_ctx.renderPass = m_renderPass;
    m_ctx.sampler = m_sampler;
    m_ctx.descriptorLayout = m_descriptorLayout;
    m_ctx.minUniformAlignment = props.limits.minUniformBufferOffsetAlignment;
    m_ctx.frame = &m_frame;
}

VkDescriptorSet VulkanDevice::GetDescriptorSet(VulkanTexture* texture) {
    const auto it = m_descriptorCache.find(texture->View());
    if (it != m_descriptorCache.end())
        return it->second;

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = m_descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_descriptorLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &ai, &set) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = m_boundUbo->Native();
    bufInfo.offset = 0;
    bufInfo.range = 64; // one mat4

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = m_sampler;
    imgInfo.imageView = texture->View();
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[0].pBufferInfo = &bufInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);

    m_descriptorCache[texture->View()] = set;
    return set;
}

void VulkanDevice::BeginFrame() {
    m_frameValid = false;
    m_boundLayout = VK_NULL_HANDLE;
    m_boundUbo = nullptr;
    m_boundTexture = nullptr;
    ++m_frame;
    if (!Valid())
        return;

    vkWaitForFences(m_device, 1, &m_inFlight, VK_TRUE, UINT64_MAX);

    const VkResult acquire = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                                   m_imageAvailable, VK_NULL_HANDLE, &m_imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        Resize(m_width, m_height);
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
        return;

    vkResetFences(m_device, 1, &m_inFlight);
    vkResetCommandBuffer(m_commandBuffer, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_commandBuffer, &begin);
    m_frameValid = true;
    m_passActive = false;
}

void VulkanDevice::Clear(const Color& color) {
    if (!m_frameValid)
        return;

    VkClearValue clear{};
    clear.color = {{color.r, color.g, color.b, color.a}};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = m_renderPass;
    rp.framebuffer = m_framebuffers[m_imageIndex];
    rp.renderArea.extent = m_swapExtent;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(m_commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // Negative-height viewport flips Y so our top-left-origin matrices match.
    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = static_cast<float>(m_swapExtent.height);
    vp.width = static_cast<float>(m_swapExtent.width);
    vp.height = -static_cast<float>(m_swapExtent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(m_commandBuffer, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, m_swapExtent};
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
    m_passActive = true;
}

void VulkanDevice::EndFrame() {
    if (!m_frameValid)
        return;
    if (m_passActive) {
        vkCmdEndRenderPass(m_commandBuffer);
        m_passActive = false;
    }
    vkEndCommandBuffer(m_commandBuffer);
}

void VulkanDevice::Present() {
    if (!m_frameValid)
        return;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &m_imageAvailable;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_commandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &m_renderFinished[m_imageIndex];
    vkQueueSubmit(m_queue, 1, &submit, m_inFlight);

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_renderFinished[m_imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapchain;
    present.pImageIndices = &m_imageIndex;
    const VkResult result = vkQueuePresentKHR(m_queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        Resize(m_width, m_height);
}

// --- Resources / draw --------------------------------------------------
std::unique_ptr<RHIBuffer> VulkanDevice::CreateBuffer(const BufferDesc& desc) {
    return std::make_unique<VulkanBuffer>(m_ctx, desc);
}
std::unique_ptr<RHITexture> VulkanDevice::CreateTexture(const TextureDesc& desc) {
    return std::make_unique<VulkanTexture>(m_ctx, desc);
}
std::unique_ptr<RHIShader> VulkanDevice::CreateShader(const ShaderDesc& desc) {
    return std::make_unique<VulkanShader>(m_ctx, desc);
}
std::unique_ptr<RHIPipeline> VulkanDevice::CreatePipeline(const PipelineDesc& desc) {
    return std::make_unique<VulkanPipeline>(m_ctx, desc);
}

void VulkanDevice::BindPipeline(RHIPipeline* pipeline) {
    if (!m_frameValid || pipeline == nullptr)
        return;
    auto* vp = static_cast<VulkanPipeline*>(pipeline);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vp->Native());
    m_boundLayout = vp->Layout();
}

void VulkanDevice::BindVertexBuffer(RHIBuffer* buffer, uint32_t, uint32_t) {
    if (!m_frameValid || buffer == nullptr)
        return;
    auto* vb = static_cast<VulkanBuffer*>(buffer);
    VkBuffer native = vb->Native();
    VkDeviceSize offset = vb->CurrentOffset();
    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &native, &offset);
}

void VulkanDevice::BindIndexBuffer(RHIBuffer* buffer, IndexFormat format) {
    if (!m_frameValid || buffer == nullptr)
        return;
    auto* vb = static_cast<VulkanBuffer*>(buffer);
    vkCmdBindIndexBuffer(m_commandBuffer, vb->Native(), 0,
                         format == IndexFormat::Uint16 ? VK_INDEX_TYPE_UINT16
                                                       : VK_INDEX_TYPE_UINT32);
}

void VulkanDevice::BindUniformBuffer(uint32_t, RHIBuffer* buffer) {
    if (buffer == nullptr)
        return;
    m_boundUbo = static_cast<VulkanBuffer*>(buffer);
    m_boundUboOffset = m_boundUbo->CurrentOffset();
}

void VulkanDevice::BindTexture(uint32_t, RHITexture* texture) {
    m_boundTexture = static_cast<VulkanTexture*>(texture);
}

void VulkanDevice::DrawIndexed(uint32_t indexCount, uint32_t firstIndex) {
    if (!m_frameValid || m_boundLayout == VK_NULL_HANDLE || m_boundUbo == nullptr ||
        m_boundTexture == nullptr)
        return;
    VkDescriptorSet set = GetDescriptorSet(m_boundTexture);
    if (set == VK_NULL_HANDLE)
        return;
    const uint32_t dynamicOffset = static_cast<uint32_t>(m_boundUboOffset);
    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boundLayout, 0, 1,
                            &set, 1, &dynamicOffset);
    vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, firstIndex, 0, 0);
}

} // namespace NaraGame

#endif // NARAGAME_HAVE_VULKAN
