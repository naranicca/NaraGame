#pragma once

#include "nara/core/Platform.h"

#if defined(NARA_PLATFORM_MACOS) || defined(NARA_PLATFORM_IOS)

#include "nara/renderer/rhi/RHIDevice.h"

namespace nara {

// TODO: Implement with Metal (MTLDevice + CAMetalLayer). The real version must
// be compiled as Objective-C++ (.mm). This stub only satisfies the interface so
// the engine links on Apple platforms.
class MetalDevice final : public RHIDevice {
public:
    explicit MetalDevice(const RHIDeviceDesc& desc) { (void)desc; }

    void Resize(uint32_t, uint32_t) override {}
    void BeginFrame() override {}
    void Clear(const Color&) override {}
    void EndFrame() override {}
    void Present() override {}

    RHIBackend Backend() const override { return RHIBackend::Metal; }
    const char* BackendName() const override { return "Metal (stub)"; }
};

} // namespace nara

#endif
