#pragma once

#include "nara/math/Mat4.h"
#include "nara/math/Vec2.h"
#include "nara/math/Vec4.h"

namespace nara {

// 2D orthographic camera for scrolling worlds. Position is the top-left of the
// view in world space (matching Renderer2D::ScreenCamera: +Y points down).
// Produces a view-projection matrix to hand to Renderer2D::Begin().
class Camera2D {
public:
    Camera2D() = default;
    Camera2D(float viewportWidth, float viewportHeight);

    void SetViewport(float width, float height) { m_viewW = width; m_viewH = height; }
    float ViewportWidth() const { return m_viewW; }
    float ViewportHeight() const { return m_viewH; }

    void SetZoom(float zoom) { m_zoom = zoom > 0.0001f ? zoom : 0.0001f; }
    float Zoom() const { return m_zoom; }

    void SetPosition(const Vec2& topLeft);
    const Vec2& Position() const { return m_position; }

    // Center the view on a world point (then clamp to bounds if set).
    void CenterOn(const Vec2& worldPoint);

    // Constrain the view to stay within [min, max] in world space.
    void SetBounds(const Vec2& min, const Vec2& max);
    void ClearBounds() { m_hasBounds = false; }

    // World units currently visible (viewport / zoom).
    Vec2 ViewSize() const { return {m_viewW / m_zoom, m_viewH / m_zoom}; }

    // Visible world rectangle as (minX, minY, maxX, maxY) — useful for culling.
    Vec4 VisibleBounds() const;

    Mat4 ViewProjection() const;

private:
    void ClampToBounds();

    float m_viewW = 1.0f;
    float m_viewH = 1.0f;
    float m_zoom = 1.0f;
    Vec2 m_position;
    bool m_hasBounds = false;
    Vec2 m_boundsMin;
    Vec2 m_boundsMax;
};

} // namespace nara
