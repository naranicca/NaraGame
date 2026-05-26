#include "NaraGame/renderer/rhi/RHIDevice.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)
    #include "renderer/rhi/d3d11/D3D11Device.h"
    #if defined(NARAGAME_HAVE_VULKAN)
        #include "renderer/rhi/vulkan/VulkanDevice.h"
        #include <string.h>
    #endif
#elif defined(NARAGAME_PLATFORM_MACOS) || defined(NARAGAME_PLATFORM_IOS)
    #include "renderer/rhi/metal/MetalDevice.h"
#elif defined(NARAGAME_PLATFORM_ANDROID)
    #include "renderer/rhi/vulkan/VulkanDevice.h"
#endif

namespace NaraGame {

std::unique_ptr<RHIDevice> RHIDevice::Create(const RHIDeviceDesc& desc) {
#if defined(NARAGAME_PLATFORM_WINDOWS)
    #if defined(NARAGAME_HAVE_VULKAN)
    // Opt into Vulkan with NARAGAME_RHI=vulkan; otherwise use Direct3D 11.
    char choice[16] = {};
    GetEnvironmentVariableA("NARAGAME_RHI", choice, sizeof(choice));
    if (_stricmp(choice, "vulkan") == 0) {
        auto vulkan = std::make_unique<VulkanDevice>(desc);
        if (vulkan->Valid()) {
            NARAGAME_INFO("RHI backend: Vulkan");
            return vulkan;
        }
        NARAGAME_WARN("Vulkan backend failed to initialize; falling back to Direct3D 11");
    }
    #endif
    return std::make_unique<D3D11Device>(desc);
#elif defined(NARAGAME_PLATFORM_MACOS) || defined(NARAGAME_PLATFORM_IOS)
    return std::make_unique<MetalDevice>(desc);
#elif defined(NARAGAME_PLATFORM_ANDROID)
    return std::make_unique<VulkanDevice>(desc);
#else
    (void)desc;
    NARAGAME_ERROR("No RHI backend available for this platform");
    return nullptr;
#endif
}

} // namespace NaraGame
