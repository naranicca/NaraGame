#pragma once

#include "NaraGame/math/Vec2.h"

#include <cstdint>

namespace NaraGame {

// Platform-neutral key identifiers. The platform layer translates native
// codes (Win32 VK, etc.) into these.
enum class Key : uint16_t {
    Unknown = 0,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0, Num1, Num2, Num3, Num4,
    Num5, Num6, Num7, Num8, Num9,

    Space, Enter, Escape, Tab, Backspace,
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown,
    LeftShift, LeftCtrl, LeftAlt,
    LeftBracket, RightBracket,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Count
};

enum class MouseButton : uint8_t {
    Left = 0,
    Right,
    Middle,
    Count
};

// Polling-style input. Game code calls IsKeyDown()/IsKeyPressed(); the platform
// window feeds state via SetKeyState(). Edge queries (Pressed/Released) compare
// against the previous frame, so NewFrame() must run once per frame before the
// window pumps events (the engine does this in Application::Run).
//
// Mouse position is reported in window client pixels (0,0 = top-left, +Y down),
// matching Renderer2D::ScreenCamera, so it can be used as world coordinates
// directly when drawing with the screen camera. For a panned/zoomed Camera2D,
// transform via the camera's inverse.
class Input {
public:
    static bool IsKeyDown(Key key);      // held this frame
    static bool IsKeyPressed(Key key);   // went down this frame
    static bool IsKeyReleased(Key key);  // went up this frame

    static bool IsMouseDown(MouseButton button);
    static bool IsMousePressed(MouseButton button);
    static bool IsMouseReleased(MouseButton button);
    static Vec2 MousePosition();

    // Accumulated scroll wheel delta for this frame, in notches (one wheel
    // detent ≈ ±1.0). Positive = scrolled toward the user / "up".
    static float MouseWheelDelta();

    // --- Called by the platform layer / engine; not for game code. ---
    static void SetKeyState(Key key, bool down);
    static void SetMouseButton(MouseButton button, bool down);
    static void SetMousePosition(float x, float y);
    static void AddMouseWheel(float deltaNotches);
    static void NewFrame();
};

} // namespace NaraGame
