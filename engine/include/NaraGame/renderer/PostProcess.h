#pragma once

#include "NaraGame/renderer/rhi/RHITypes.h"

#include <cstdint>
#include <memory>

namespace NaraGame {

class RHIBuffer;
class RHIDevice;
class RHIPipeline;
class RHIShader;
class RHITexture;

// Post-process knobs. Defaults give a mild cinematic look -- bloom on
// the highlights, focus around 5 world units out, full ACES tone map.
struct PostProcessParams {
    // Bloom: brightness threshold past which pixels start blooming, and
    // a multiplier on the blurred-bright sample when composited back.
    float bloomThreshold = 1.0f;
    float bloomStrength  = 0.6f;
    // Depth of field: the world-space distance in front of the camera
    // that stays sharp, plus the half-range either side of focus over
    // which the blur ramps to full strength. Set focusRange very large
    // to effectively disable DoF without changing pipeline state.
    float focusDistance  = 5.0f;
    float focusRange     = 100.0f;
    // Final exposure multiplier applied before the ACES curve. >1
    // brightens, <1 darkens; ACES handles the highlight roll-off so
    // mild over-exposure stays legible.
    float exposure       = 1.0f;
};

// Offscreen scene render + bloom + tone-map + DoF pipeline. Caller
// pattern:
//   post.BindSceneTarget();    // OM: HDR colour + own depth
//   device.Clear(skyColor);
//   device.ClearDepth(1.0f);
//   renderer3D.Begin/DrawMesh/End(...);
//   post.RunPost(params);      // bright -> blur H -> blur V -> ACES
//                              // composite straight to the backbuffer
//
// PostProcess owns five render targets: an RGBA16F scene HDR RT, a
// matching Depth24 RT with SRV access, a half-res RGBA16F bright-pass
// RT, and two half-res RGBA16F blur RTs. The final composite pass
// reads the HDR + bloom + depth and emits LDR straight to the
// backbuffer.
class PostProcess {
public:
    PostProcess() = default;
    ~PostProcess();
    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    // Allocate the HDR + depth + bloom RTs and compile the post pipelines.
    // `width` / `height` should match the backbuffer; resize the
    // PostProcess on window resize.
    void Init(RHIDevice& device, uint32_t width, uint32_t height);
    void Shutdown();

    // Recreate the RTs at the new dimensions. Pipelines and shaders
    // survive -- only the textures are reallocated.
    void Resize(uint32_t width, uint32_t height);

    // Bind the HDR scene RT + matching depth so the next draws land in
    // the post-process domain instead of writing to the backbuffer.
    void BindSceneTarget();

    // Accessors for the underlying scene textures. Renderer3D::Begin
    // takes these so the main pass routes back to the HDR / post depth
    // after its shadow pass swaps the RT.
    RHITexture* SceneColor() const { return m_sceneHDR.get(); }
    RHITexture* SceneDepth() const { return m_sceneDepth.get(); }

    // Run the bright-pass / two-pass Gaussian blur / final composite,
    // ending with the LDR result written to the backbuffer. Sets the
    // render target to the backbuffer when it returns.
    void RunPost(const PostProcessParams& params = {});

private:
    void CreateRTs();
    void CreatePipelines();
    void DrawFullscreen();

    RHIDevice* m_device = nullptr;
    uint32_t   m_width  = 0;
    uint32_t   m_height = 0;

    // RTs are recreated on Resize.
    std::unique_ptr<RHITexture> m_sceneHDR;
    std::unique_ptr<RHITexture> m_sceneDepth;
    std::unique_ptr<RHITexture> m_bloomBright;
    std::unique_ptr<RHITexture> m_bloomBlurH;
    std::unique_ptr<RHITexture> m_bloomBlurV;

    // One fullscreen-triangle VB/IB shared by every post pass.
    std::unique_ptr<RHIBuffer>  m_quadVB;
    std::unique_ptr<RHIBuffer>  m_quadIB;
    std::unique_ptr<RHIBuffer>  m_postUB;

    std::unique_ptr<RHIShader>   m_brightShader;
    std::unique_ptr<RHIShader>   m_blurShader;
    std::unique_ptr<RHIShader>   m_compositeShader;
    std::unique_ptr<RHIPipeline> m_brightPipeline;
    std::unique_ptr<RHIPipeline> m_blurPipeline;
    std::unique_ptr<RHIPipeline> m_compositePipeline;
};

} // namespace NaraGame
