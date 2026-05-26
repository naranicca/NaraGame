#pragma once

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
    LeftShift, LeftCtrl, LeftAlt,

    Count
};

// Polling-style input. Game code calls IsKeyDown()/IsKeyPressed(); the platform
// window feeds state via SetKeyState(). Edge queries (Pressed/Released) compare
// against the previous frame, so NewFrame() must run once per frame before the
// window pumps events (the engine does this in Application::Run).
class Input {
public:
    static bool IsKeyDown(Key key);      // held this frame
    static bool IsKeyPressed(Key key);   // went down this frame
    static bool IsKeyReleased(Key key);  // went up this frame

    // --- Called by the platform layer / engine; not for game code. ---
    static void SetKeyState(Key key, bool down);
    static void NewFrame();
};

} // namespace NaraGame
