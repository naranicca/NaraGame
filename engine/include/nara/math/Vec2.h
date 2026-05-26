#pragma once

#include <cmath>

namespace nara {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr explicit Vec2(float s) : x(s), y(s) {}
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator-() const { return {-x, -y}; }

    constexpr Vec2& operator+=(const Vec2& v) { x += v.x; y += v.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& v) { x -= v.x; y -= v.y; return *this; }
    constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr Vec2& operator/=(float s) { x /= s; y /= s; return *this; }
};

constexpr Vec2 operator+(const Vec2& a, const Vec2& b) { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(const Vec2& a, const Vec2& b) { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator*(const Vec2& a, const Vec2& b) { return {a.x * b.x, a.y * b.y}; }
constexpr Vec2 operator*(const Vec2& v, float s) { return {v.x * s, v.y * s}; }
constexpr Vec2 operator*(float s, const Vec2& v) { return {v.x * s, v.y * s}; }
constexpr Vec2 operator/(const Vec2& v, float s) { return {v.x / s, v.y / s}; }

constexpr float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

inline float Length(const Vec2& v) { return std::sqrt(Dot(v, v)); }
constexpr float LengthSq(const Vec2& v) { return Dot(v, v); }

inline Vec2 Normalize(const Vec2& v) {
    const float len = Length(v);
    return len > 0.0f ? v / len : Vec2{};
}

} // namespace nara
