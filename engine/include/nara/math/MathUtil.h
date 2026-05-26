#pragma once

namespace nara {

inline constexpr float Pi = 3.14159265358979323846f;
inline constexpr float TwoPi = 2.0f * Pi;
inline constexpr float HalfPi = 0.5f * Pi;

constexpr float DegToRad(float degrees) { return degrees * (Pi / 180.0f); }
constexpr float RadToDeg(float radians) { return radians * (180.0f / Pi); }

template <typename T>
constexpr T Min(T a, T b) { return a < b ? a : b; }

template <typename T>
constexpr T Max(T a, T b) { return a > b ? a : b; }

template <typename T>
constexpr T Clamp(T value, T lo, T hi) { return Min(Max(value, lo), hi); }

template <typename T>
constexpr T Lerp(T a, T b, float t) { return a + (b - a) * t; }

} // namespace nara
