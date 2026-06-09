#include "NaraGame/input/Input.h"

#include <array>
#include <cstddef>
#include <string>

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
}

} // namespace NaraGame
