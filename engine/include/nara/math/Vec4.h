#pragma once

#include "nara/math/Vec3.h"

#include <cmath>

namespace nara {

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() = default;
    constexpr explicit Vec4(float s) : x(s), y(s), z(s), w(s) {}
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    constexpr Vec3 xyz() const { return {x, y, z}; }

    constexpr Vec4& operator+=(const Vec4& v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
    constexpr Vec4& operator-=(const Vec4& v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
    constexpr Vec4& operator*=(float s) { x *= s; y *= s; z *= s; w *= s; return *this; }
};

constexpr Vec4 operator+(const Vec4& a, const Vec4& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
constexpr Vec4 operator-(const Vec4& a, const Vec4& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
constexpr Vec4 operator*(const Vec4& v, float s) { return {v.x * s, v.y * s, v.z * s, v.w * s}; }
constexpr Vec4 operator*(float s, const Vec4& v) { return {v.x * s, v.y * s, v.z * s, v.w * s}; }

constexpr float Dot(const Vec4& a, const Vec4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

} // namespace nara
