#include "NaraGame/input/Input.h"

#include <array>
#include <cstddef>
#include <string>

#if defined(NARAGAME_PLATFORM_WINDOWS)
#  include <Windows.h>
#  include <Xinput.h>
#endif

namespace NaraGame {

// "Current" reflects the held state for IsKeyDown/IsMouseDown. Edge queries
// (IsKeyPressed/IsKeyReleased and their mouse counterparts) read from the
// "pressed/released this frame" trackers, which OR in any down/up event
// since the last NewFrame() and survive a sub-frame down→up cycle (a fast
// synthesized click, or a real human click that completes between two game
// frames). Without those trackers the naive current-vs-previous edge would
// silently drop sub-frame events.

static std::array<bool, static_cast<std::size_t>(Key::Count)> g_keyCurrent{};
static std::array<bool, static_cast<std::size_t>(Key::Count)> g_keyPressedThisFrame{};
static std::array<bool, static_cast<std::size_t>(Key::Count)> g_keyReleasedThisFrame{};

static std::array<bool, static_cast<std::size_t>(MouseButton::Count)> g_mouseCurrent{};
static std::array<bool, static_cast<std::size_t>(MouseButton::Count)> g_mousePressedThisFrame{};
static std::array<bool, static_cast<std::size_t>(MouseButton::Count)> g_mouseReleasedThisFrame{};
static Vec2  g_mousePosition{};
static float g_mouseWheelThisFrame = 0.0f;
static std::string g_textInputThisFrame;
static constexpr std::size_t kGamepadCount = 4;
static constexpr std::size_t kGamepadButtons = static_cast<std::size_t>(GamepadButton::Count);
static std::array<bool, kGamepadCount> g_padConnected{};
static std::array<std::array<bool, kGamepadButtons>, kGamepadCount> g_padCurrent{};
static std::array<std::array<bool, kGamepadButtons>, kGamepadCount> g_padPrevious{};
static std::array<std::array<float, 6>, kGamepadCount> g_padAxes{};

static bool ValidPad(int index) { return index >= 0 && index < int(kGamepadCount); }

#if defined(NARAGAME_PLATFORM_WINDOWS)
static void PollGamepads() {
    static constexpr WORD masks[kGamepadButtons] = {
        XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
        XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT};
    auto stick = [](SHORT value, SHORT deadzone) {
        if (value > -deadzone && value < deadzone) return 0.0f;
        const float denom = value < 0 ? 32768.0f : 32767.0f;
        return float(value) / denom;
    };
    for (DWORD i = 0; i < kGamepadCount; ++i) {
        XINPUT_STATE state{};
        g_padConnected[i] = XInputGetState(i, &state) == ERROR_SUCCESS;
        if (!g_padConnected[i]) {
            g_padCurrent[i].fill(false); g_padAxes[i].fill(0.0f); continue;
        }
        for (std::size_t b = 0; b < kGamepadButtons; ++b)
            g_padCurrent[i][b] = (state.Gamepad.wButtons & masks[b]) != 0;
        g_padAxes[i] = {
            stick(state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
            -stick(state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
            stick(state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
            -stick(state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
            state.Gamepad.bLeftTrigger < XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 0.0f
                : float(state.Gamepad.bLeftTrigger) / 255.0f,
            state.Gamepad.bRightTrigger < XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 0.0f
                : float(state.Gamepad.bRightTrigger) / 255.0f};
    }
}
#endif

bool Input::IsKeyDown(Key key) {
    return g_keyCurrent[static_cast<std::size_t>(key)];
}

bool Input::IsKeyPressed(Key key) {
    return g_keyPressedThisFrame[static_cast<std::size_t>(key)];
}

bool Input::IsKeyReleased(Key key) {
    return g_keyReleasedThisFrame[static_cast<std::size_t>(key)];
}

bool Input::IsMouseDown(MouseButton button) {
    return g_mouseCurrent[static_cast<std::size_t>(button)];
}

bool Input::IsMousePressed(MouseButton button) {
    return g_mousePressedThisFrame[static_cast<std::size_t>(button)];
}

bool Input::IsMouseReleased(MouseButton button) {
    return g_mouseReleasedThisFrame[static_cast<std::size_t>(button)];
}

Vec2 Input::MousePosition() {
    return g_mousePosition;
}

float Input::MouseWheelDelta() {
    return g_mouseWheelThisFrame;
}

bool Input::IsGamepadConnected(int index) {
    return ValidPad(index) && g_padConnected[std::size_t(index)];
}
bool Input::IsGamepadDown(int index, GamepadButton button) {
    return ValidPad(index) && g_padCurrent[std::size_t(index)][std::size_t(button)];
}
bool Input::IsGamepadPressed(int index, GamepadButton button) {
    return ValidPad(index) && IsGamepadDown(index, button) &&
        !g_padPrevious[std::size_t(index)][std::size_t(button)];
}
bool Input::IsGamepadReleased(int index, GamepadButton button) {
    return ValidPad(index) && !IsGamepadDown(index, button) &&
        g_padPrevious[std::size_t(index)][std::size_t(button)];
}
float Input::GamepadAxisValue(int index, GamepadAxis axis) {
    return ValidPad(index) ? g_padAxes[std::size_t(index)][std::size_t(axis)] : 0.0f;
}

void Input::SetKeyState(Key key, bool down) {
    const std::size_t i = static_cast<std::size_t>(key);
    // Edge-event flag OR'd in so a same-frame down+up pair (or a same-frame
    // up+down on auto-repeat) still registers as a press / release.
    if (down) g_keyPressedThisFrame[i] = true;
    else      g_keyReleasedThisFrame[i] = true;
    g_keyCurrent[i] = down;
}

void Input::SetMouseButton(MouseButton button, bool down) {
    const std::size_t i = static_cast<std::size_t>(button);
    if (down) g_mousePressedThisFrame[i] = true;
    else      g_mouseReleasedThisFrame[i] = true;
    g_mouseCurrent[i] = down;
}

void Input::SetMousePosition(float x, float y) {
    g_mousePosition = {x, y};
}
void Input::SetGamepadConnected(int index, bool connected) {
    if (!ValidPad(index)) return;
    g_padConnected[std::size_t(index)] = connected;
    if (!connected) {
        g_padCurrent[std::size_t(index)].fill(false);
        g_padAxes[std::size_t(index)].fill(0.0f);
    }
}
void Input::SetGamepadButton(int index, GamepadButton button, bool down) {
    if (!ValidPad(index)) return;
    g_padCurrent[std::size_t(index)][std::size_t(button)] = down;
}
void Input::SetGamepadAxis(int index, GamepadAxis axis, float value) {
    if (!ValidPad(index)) return;
    const bool trigger = axis == GamepadAxis::LeftTrigger ||
                         axis == GamepadAxis::RightTrigger;
    const float deadzone = trigger ? 0.05f : 0.15f;
    if (value > -deadzone && value < deadzone) value = 0.0f;
    if (value < -1.0f) value = -1.0f;
    if (value >  1.0f) value =  1.0f;
    g_padAxes[std::size_t(index)][std::size_t(axis)] = value;
}

void Input::AddMouseWheel(float deltaNotches) {
    // Wheel events can fire multiple times between two game frames; sum so
    // the frame sees the total scroll regardless of OS coalescing.
    g_mouseWheelThisFrame += deltaNotches;
}

const std::string& Input::TextInputThisFrame() {
    return g_textInputThisFrame;
}

void Input::PushTextInput(char c) {
    g_textInputThisFrame.push_back(c);
}

void Input::NewFrame() {
    // Reset edge-event trackers; "current" carries over because Down state
    // is continuous (button stays held across frames until an up event).
    g_keyPressedThisFrame.fill(false);
    g_keyReleasedThisFrame.fill(false);
    g_mousePressedThisFrame.fill(false);
    g_mouseReleasedThisFrame.fill(false);
    g_mouseWheelThisFrame = 0.0f;
    g_textInputThisFrame.clear();
    g_padPrevious = g_padCurrent;
#if defined(NARAGAME_PLATFORM_WINDOWS)
    PollGamepads();
#endif
}

} // namespace NaraGame
