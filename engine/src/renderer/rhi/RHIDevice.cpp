#include "nara/renderer/rhi/RHIDevice.h"

#include "nara/core/Log.h"
#include "nara/core/Platform.h"

#if defined(NARA_PLATFORM_WINDOWS)
    #include "renderer/rhi/d3d11/D3D11Device.h"
    #if defined(NARA_HAVE_VULKAN)
        #include "renderer/rhi/vulkan/VulkanDevice.h"
        #include <string.h>
    #endif
#elif defined(NARA_PLATFORM_MACOS) || defined(NARA_PLATFORM_IOS)
    #include "renderer/rhi/metal/MetalDevice.h"
#elif defined(NARA_PLATFORM_ANDROID)
    #include "renderer/rhi/vulkan/VulkanDevice.h"
#endif

namespace nara {

std::unique_ptr<RHIDevice> RHIDevice::Create(const RHIDeviceDesc& desc) {
#if defined(NARA_PLATFORM_WINDOWS)
    #if defined(NARA_HAVE_VULKAN)
    // Opt into Vulkan with NARA_RHI=vulkan; otherwise use Direct3D 11.
    char choice[16] = {};
    GetEnvironmentVariableA("NARA_RHI", choice, sizeof(choice));
    if (_stricmp(choice, "vulkan") == 0) {
        auto vulkan = std::make_unique<VulkanDevice>(desc);
        if (vulkan->Valid()) {
            NARA_INFO("RHI backend: Vulkan");
            return vulkan;
        }
        NARA_WARN("Vulkan backend failed to initialize; falling back to Direct3D 11");
    }
    #endif
    return std::make_unique<D3D11Device>(desc);
#elif defined(NARA_PLATFORM_MACOS) || defined(NARA_PLATFORM_IOS)
    return std::make_unique<MetalDevice>(desc);
#elif defined(NARA_PLATFORM_ANDROID)
    return std::make_unique<VulkanDevice>(desc);
#else
    (void)desc;
    NARA_ERROR("No RHI backend available for this platform");
    return nullptr;
#endif
}

} // namespace nara
