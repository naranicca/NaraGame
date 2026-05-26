#pragma once

#include <cmath>

namespace NaraGame {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr explicit Vec3(float s) : x(s), y(s), z(s) {}
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator-() const { return {-x, -y, -z}; }

    constexpr Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    constexpr Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    constexpr Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
};

constexpr Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator*(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
constexpr Vec3 operator*(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
constexpr Vec3 operator*(float s, const Vec3& v) { return {v.x * s, v.y * s, v.z * s}; }
constexpr Vec3 operator/(const Vec3& v, float s) { return {v.x / s, v.y / s, v.z / s}; }

constexpr float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
constexpr float LengthSq(const Vec3& v) { return Dot(v, v); }

inline Vec3 Normalize(const Vec3& v) {
    const float len = Length(v);
    return len > 0.0f ? v / len : Vec3{};
}

} // namespace NaraGame
