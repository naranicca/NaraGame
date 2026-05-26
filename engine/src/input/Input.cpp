#include "nara/input/Input.h"

#include <array>
#include <cstddef>

namespace nara {

static std::array<bool, static_cast<std::size_t>(Key::Count)> g_current{};
static std::array<bool, static_cast<std::size_t>(Key::Count)> g_previous{};

bool Input::IsKeyDown(Key key) {
    return g_current[static_cast<std::size_t>(key)];
}

bool Input::IsKeyPressed(Key key) {
    const std::size_t i = static_cast<std::size_t>(key);
    return g_current[i] && !g_previous[i];
}

bool Input::IsKeyReleased(Key key) {
    const std::size_t i = static_cast<std::size_t>(key);
    return !g_current[i] && g_previous[i];
}

void Input::SetKeyState(Key key, bool down) {
    g_current[static_cast<std::size_t>(key)] = down;
}

void Input::NewFrame() {
    g_previous = g_current; // snapshot last frame for edge detection
}

} // namespace nara
