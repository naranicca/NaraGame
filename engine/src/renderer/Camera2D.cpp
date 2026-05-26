#include "nara/renderer/Camera2D.h"

namespace nara {

Camera2D::Camera2D(float viewportWidth, float viewportHeight)
    : m_viewW(viewportWidth), m_viewH(viewportHeight) {}

void Camera2D::SetPosition(const Vec2& topLeft) {
    m_position = topLeft;
    ClampToBounds();
}

void Camera2D::CenterOn(const Vec2& worldPoint) {
    const Vec2 view = ViewSize();
    m_position = {worldPoint.x - view.x * 0.5f, worldPoint.y - view.y * 0.5f};
    ClampToBounds();
}

void Camera2D::SetBounds(const Vec2& min, const Vec2& max) {
    m_hasBounds = true;
    m_boundsMin = min;
    m_boundsMax = max;
    ClampToBounds();
}

void Camera2D::ClampToBounds() {
    if (!m_hasBounds)
        return;

    const Vec2 view = ViewSize();

    const float maxX = m_boundsMax.x - view.x;
    m_position.x = (maxX < m_boundsMin.x) ? m_boundsMin.x // world narrower than view
                                          : Clamp(m_position.x, m_boundsMin.x, maxX);

    const float maxY = m_boundsMax.y - view.y;
    m_position.y = (maxY < m_boundsMin.y) ? m_boundsMin.y
                                          : Clamp(m_position.y, m_boundsMin.y, maxY);
}

Vec4 Camera2D::VisibleBounds() const {
    const Vec2 view = ViewSize();
    return {m_position.x, m_position.y, m_position.x + view.x, m_position.y + view.y};
}

Mat4 Camera2D::ViewProjection() const {
    const Vec2 view = ViewSize();
    return mat4::Ortho(m_position.x, m_position.x + view.x, m_position.y + view.y, m_position.y,
                       -1.0f, 1.0f);
}

} // namespace nara
