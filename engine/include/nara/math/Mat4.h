#pragma once

#include "nara/math/MathUtil.h"
#include "nara/math/Vec3.h"
#include "nara/math/Vec4.h"

#include <cmath>

namespace nara {

// 4x4 matrix, column-major storage (same layout the GPU expects: data[col*4 + row]).
// Projection helpers target a [0, 1] depth range (Direct3D / Vulkan / Metal) and
// a right-handed coordinate system.
struct Mat4 {
    // Column-major: element (row, col) lives at data[col * 4 + row].
    float data[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    constexpr Mat4() = default;

    float& At(int row, int col) { return data[col * 4 + row]; }
    float At(int row, int col) const { return data[col * 4 + row]; }

    static constexpr Mat4 Identity() { return Mat4{}; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a.At(row, k) * b.At(k, col);
            r.At(row, col) = sum;
        }
    }
    return r;
}

inline Vec4 operator*(const Mat4& m, const Vec4& v) {
    return {
        m.At(0, 0) * v.x + m.At(0, 1) * v.y + m.At(0, 2) * v.z + m.At(0, 3) * v.w,
        m.At(1, 0) * v.x + m.At(1, 1) * v.y + m.At(1, 2) * v.z + m.At(1, 3) * v.w,
        m.At(2, 0) * v.x + m.At(2, 1) * v.y + m.At(2, 2) * v.z + m.At(2, 3) * v.w,
        m.At(3, 0) * v.x + m.At(3, 1) * v.y + m.At(3, 2) * v.z + m.At(3, 3) * v.w,
    };
}

namespace mat4 {

inline Mat4 Translation(const Vec3& t) {
    Mat4 m;
    m.At(0, 3) = t.x;
    m.At(1, 3) = t.y;
    m.At(2, 3) = t.z;
    return m;
}

inline Mat4 Scale(const Vec3& s) {
    Mat4 m;
    m.At(0, 0) = s.x;
    m.At(1, 1) = s.y;
    m.At(2, 2) = s.z;
    return m;
}

inline Mat4 RotationZ(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 m;
    m.At(0, 0) = c; m.At(0, 1) = -s;
    m.At(1, 0) = s; m.At(1, 1) = c;
    return m;
}

inline Mat4 RotationX(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 m;
    m.At(1, 1) = c; m.At(1, 2) = -s;
    m.At(2, 1) = s; m.At(2, 2) = c;
    return m;
}

inline Mat4 RotationY(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 m;
    m.At(0, 0) = c;  m.At(0, 2) = s;
    m.At(2, 0) = -s; m.At(2, 2) = c;
    return m;
}

// Right-handed orthographic projection with depth in [0, 1]. Ideal for 2D.
inline Mat4 Ortho(float left, float right, float bottom, float top, float near, float far) {
    Mat4 m;
    m.At(0, 0) = 2.0f / (right - left);
    m.At(1, 1) = 2.0f / (top - bottom);
    m.At(2, 2) = -1.0f / (far - near);
    m.At(0, 3) = -(right + left) / (right - left);
    m.At(1, 3) = -(top + bottom) / (top - bottom);
    m.At(2, 3) = -near / (far - near);
    return m;
}

// Right-handed perspective projection with depth in [0, 1]. fovY in radians.
inline Mat4 Perspective(float fovY, float aspect, float near, float far) {
    const float tanHalf = std::tan(fovY * 0.5f);
    Mat4 m;
    m.data[0] = 0.0f; m.data[5] = 0.0f; m.data[10] = 0.0f; m.data[15] = 0.0f;
    m.At(0, 0) = 1.0f / (aspect * tanHalf);
    m.At(1, 1) = 1.0f / tanHalf;
    m.At(2, 2) = far / (near - far);
    m.At(3, 2) = -1.0f;
    m.At(2, 3) = -(far * near) / (far - near);
    return m;
}

// Right-handed view matrix looking from `eye` toward `center`.
inline Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    const Vec3 f = Normalize(center - eye);
    const Vec3 s = Normalize(Cross(f, up));
    const Vec3 u = Cross(s, f);

    Mat4 m;
    m.At(0, 0) = s.x; m.At(0, 1) = s.y; m.At(0, 2) = s.z;
    m.At(1, 0) = u.x; m.At(1, 1) = u.y; m.At(1, 2) = u.z;
    m.At(2, 0) = -f.x; m.At(2, 1) = -f.y; m.At(2, 2) = -f.z;
    m.At(0, 3) = -Dot(s, eye);
    m.At(1, 3) = -Dot(u, eye);
    m.At(2, 3) = Dot(f, eye);
    return m;
}

} // namespace mat4
} // namespace nara
