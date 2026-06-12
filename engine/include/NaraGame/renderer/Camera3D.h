#pragma once

#include "NaraGame/math/Mat4.h"
#include "NaraGame/math/Vec3.h"

namespace NaraGame {

// Free-look perspective camera for 3D scenes. Stores a position + look-at
// point + up vector and bakes them into View / Projection matrices on
// demand. Right-handed, with depth in [0, 1] to match the engine's
// Mat4::Perspective convention (Direct3D + Vulkan + Metal default).
//
// Typical usage:
//   Camera3D cam;
//   cam.SetViewport(w, h);
//   cam.SetPosition({0, 1.5, 4});
//   cam.LookAt({0, 0, 0});
//   renderer3D.Begin(cam.View(), cam.Projection());
//
// Aspect is recomputed from viewport width/height each time Projection()
// runs, so on window resize you only need to call SetViewport again.
class Camera3D {
public:
    Camera3D() = default;

    void SetViewport(float width, float height) {
        m_viewW = width;
        m_viewH = height > 0.0f ? height : 1.0f;
    }
    float ViewportWidth() const { return m_viewW; }
    float ViewportHeight() const { return m_viewH; }

    void SetFovY(float radians) { m_fovY = radians; }
    float FovY() const { return m_fovY; }

    void SetNearFar(float nearZ, float farZ) {
        m_near = nearZ;
        m_far  = farZ;
    }
    float Near() const { return m_near; }
    float Far()  const { return m_far; }

    void SetPosition(const Vec3& p) { m_position = p; }
    const Vec3& Position() const { return m_position; }

    void LookAt(const Vec3& target, const Vec3& up = {0, 1, 0}) {
        m_target = target;
        m_up     = up;
    }
    const Vec3& Target() const { return m_target; }
    const Vec3& Up()     const { return m_up; }

    Mat4 View() const {
        return mat4::LookAt(m_position, m_target, m_up);
    }
    Mat4 Projection() const {
        const float aspect = m_viewW / m_viewH;
        return mat4::Perspective(m_fovY, aspect, m_near, m_far);
    }
    Mat4 ViewProjection() const { return Projection() * View(); }

private:
    float m_viewW = 1.0f;
    float m_viewH = 1.0f;
    float m_fovY  = 1.0472f;  // 60 deg
    float m_near  = 0.1f;
    float m_far   = 100.0f;
    Vec3  m_position{0.0f, 0.0f, 4.0f};
    Vec3  m_target  {0.0f, 0.0f, 0.0f};
    Vec3  m_up      {0.0f, 1.0f, 0.0f};
};

} // namespace NaraGame
