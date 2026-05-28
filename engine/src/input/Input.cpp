#include "NaraGame/input/Input.h"

#include <array>
#include <cstddef>

namespace NaraGame {

static std::array<bool, static_cast<std::size_t>(Key::Count)> g_keyCurrent{};
static std::array<bool, static_cast<std::size_t>(Key::Count)> g_keyPrevious{};

static std::array<bool, static_cast<std::size_t>(MouseButton::Count)> g_mouseCurrent{};
static std::array<bool, static_cast<std::size_t>(MouseButton::Count)> g_mousePrevious{};
static Vec2 g_mousePosition{};

bool Input::IsKeyDown(Key key) {
    return g_keyCurrent[static_cast<std::size_t>(key)];
}

bool Input::IsKeyPressed(Key key) {
    const std::size_t i = static_cast<std::size_t>(key);
    return g_keyCurrent[i] && !g_keyPrevious[i];
}

bool Input::IsKeyReleased(Key key) {
    const std::size_t i = static_cast<std::size_t>(key);
    return !g_keyCurrent[i] && g_keyPrevious[i];
}

bool Input::IsMouseDown(MouseButton button) {
    return g_mouseCurrent[static_cast<std::size_t>(button)];
}

bool Input::IsMousePressed(MouseButton button) {
    const std::size_t i = static_cast<std::size_t>(button);
    return g_mouseCurrent[i] && !g_mousePrevious[i];
}

bool Input::IsMouseReleased(MouseButton button) {
    const std::size_t i = static_cast<std::size_t>(button);
    return !g_mouseCurrent[i] && g_mousePrevious[i];
}

Vec2 Input::MousePosition() {
    return g_mousePosition;
}

void Input::SetKeyState(Key key, bool down) {
    g_keyCurrent[static_cast<std::size_t>(key)] = down;
}

void Input::SetMouseButton(MouseButton button, bool down) {
    g_mouseCurrent[static_cast<std::size_t>(button)] = down;
}

void Input::SetMousePosition(float x, float y) {
    g_mousePosition = {x, y};
}

void Input::NewFrame() {
    g_keyPrevious = g_keyCurrent;     // snapshot last frame for edge detection
    g_mousePrevious = g_mouseCurrent; // (mouse position is continuous, no snapshot)
}

} // namespace NaraGame
