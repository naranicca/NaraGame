#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_MACOS) || defined(NARAGAME_PLATFORM_IOS)

#include "NaraGame/renderer/rhi/RHIDevice.h"

namespace NaraGame {

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
    void SetRenderTarget(RHITexture*) override {}

    RHIBackend Backend() const override { return RHIBackend::Metal; }
    const char* BackendName() const override { return "Metal (stub)"; }
};

} // namespace NaraGame

#endif
