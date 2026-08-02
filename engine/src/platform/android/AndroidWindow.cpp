#include "platform/android/AndroidWindow.h"

#if defined(NARAGAME_PLATFORM_ANDROID)

#include "NaraGame/core/Log.h"
#include "NaraGame/input/Input.h"

#include <android/input.h>
#include <android/looper.h>

namespace NaraGame {
namespace {
android_app* g_androidApp = nullptr;

Key TranslateAndroidKey(int32_t code) {
    if (code >= AKEYCODE_A && code <= AKEYCODE_Z) {
        return static_cast<Key>(static_cast<int>(Key::A) + code - AKEYCODE_A);
    }
    if (code >= AKEYCODE_0 && code <= AKEYCODE_9) {
        return static_cast<Key>(static_cast<int>(Key::Num0) + code - AKEYCODE_0);
    }
    switch (code) {
        case AKEYCODE_SPACE:       return Key::Space;
        case AKEYCODE_ENTER:       return Key::Enter;
        case AKEYCODE_BACK:        return Key::Escape;
        case AKEYCODE_TAB:         return Key::Tab;
        case AKEYCODE_DEL:         return Key::Backspace;
        case AKEYCODE_DPAD_LEFT:   return Key::Left;
        case AKEYCODE_DPAD_RIGHT:  return Key::Right;
        case AKEYCODE_DPAD_UP:     return Key::Up;
        case AKEYCODE_DPAD_DOWN:   return Key::Down;
        case AKEYCODE_MOVE_HOME:   return Key::Home;
        case AKEYCODE_MOVE_END:    return Key::End;
        default:                   return Key::Unknown;
    }
}

bool TranslateGamepadKey(int32_t code, GamepadButton& button) {
    switch (code) {
        case AKEYCODE_BUTTON_A: button = GamepadButton::A; return true;
        case AKEYCODE_BUTTON_B: button = GamepadButton::B; return true;
        case AKEYCODE_BUTTON_X: button = GamepadButton::X; return true;
        case AKEYCODE_BUTTON_Y: button = GamepadButton::Y; return true;
        case AKEYCODE_BUTTON_L1: button = GamepadButton::LeftShoulder; return true;
        case AKEYCODE_BUTTON_R1: button = GamepadButton::RightShoulder; return true;
        case AKEYCODE_BUTTON_SELECT: button = GamepadButton::Back; return true;
        case AKEYCODE_BUTTON_START: button = GamepadButton::Start; return true;
        case AKEYCODE_BUTTON_THUMBL: button = GamepadButton::LeftStick; return true;
        case AKEYCODE_BUTTON_THUMBR: button = GamepadButton::RightStick; return true;
        default: return false;
    }
}
} // namespace

void SetAndroidApp(android_app* app) {
    g_androidApp = app;
}

android_app* GetAndroidApp() { return g_androidApp; }

AndroidWindow::AndroidWindow(const WindowDesc& desc)
    : m_desc(desc), m_app(g_androidApp) {
    if (m_app == nullptr) {
        NARAGAME_ERROR("AndroidWindow: android_app was not initialized");
        m_shouldClose = true;
        return;
    }

    m_app->userData = this;
    m_app->onAppCmd = &AndroidWindow::HandleCommand;
    m_app->onInputEvent = &AndroidWindow::HandleInput;

    // NativeActivity may call android_main before APP_CMD_INIT_WINDOW. Pump
    // lifecycle messages until an ANativeWindow is ready for Vulkan.
    while (m_app->window == nullptr && !m_app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        const int ident = ALooper_pollOnce(-1, nullptr, &events,
                                            reinterpret_cast<void**>(&source));
        if (ident >= 0 && source != nullptr) source->process(m_app, source);
    }
    RefreshNativeWindow();
}

AndroidWindow::~AndroidWindow() {
    if (m_app != nullptr && m_app->userData == this) {
        m_app->userData = nullptr;
        m_app->onAppCmd = nullptr;
        m_app->onInputEvent = nullptr;
    }
}

void AndroidWindow::RefreshNativeWindow() {
    m_nativeWindow = m_app != nullptr ? m_app->window : nullptr;
    auto* window = static_cast<ANativeWindow*>(m_nativeWindow);
    if (window != nullptr) {
        m_width = static_cast<uint32_t>(ANativeWindow_getWidth(window));
        m_height = static_cast<uint32_t>(ANativeWindow_getHeight(window));
    } else {
        m_width = 0;
        m_height = 0;
    }
}

void AndroidWindow::HandleCommand(android_app* app, int32_t command) {
    auto* self = static_cast<AndroidWindow*>(app->userData);
    if (self == nullptr) return;
    switch (command) {
        case APP_CMD_INIT_WINDOW:
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            self->RefreshNativeWindow();
            break;
        case APP_CMD_TERM_WINDOW:
            self->m_nativeWindow = nullptr;
            self->m_width = self->m_height = 0;
            break;
        case APP_CMD_DESTROY:
            self->m_shouldClose = true;
            break;
        default:
            break;
    }
}

int32_t AndroidWindow::HandleInput(android_app* app, AInputEvent* event) {
    auto* self = static_cast<AndroidWindow*>(app->userData);
    if (self == nullptr) return 0;

    const int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        GamepadButton padButton{};
        if (TranslateGamepadKey(AKeyEvent_getKeyCode(event), padButton)) {
            Input::SetGamepadConnected(0, true);
            Input::SetGamepadButton(0, padButton,
                AKeyEvent_getAction(event) != AKEY_EVENT_ACTION_UP);
            return 1;
        }
        const Key key = TranslateAndroidKey(AKeyEvent_getKeyCode(event));
        if (key == Key::Unknown) return 0;
        const int32_t action = AKeyEvent_getAction(event);
        Input::SetKeyState(key, action != AKEY_EVENT_ACTION_UP);
        return 1;
    }
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t source = AInputEvent_getSource(event);
        if ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK ||
            (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) {
            Input::SetGamepadConnected(0, true);
            Input::SetGamepadAxis(0, GamepadAxis::LeftX,
                                  AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0));
            Input::SetGamepadAxis(0, GamepadAxis::LeftY,
                                  AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0));
            Input::SetGamepadAxis(0, GamepadAxis::RightX,
                                  AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0));
            Input::SetGamepadAxis(0, GamepadAxis::RightY,
                                  AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0));
            Input::SetGamepadAxis(0, GamepadAxis::LeftTrigger,
                                  AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0));
            Input::SetGamepadAxis(0, GamepadAxis::RightTrigger,
                                  AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0));
            const float hatX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
            const float hatY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
            Input::SetGamepadButton(0, GamepadButton::DpadLeft, hatX < -0.5f);
            Input::SetGamepadButton(0, GamepadButton::DpadRight, hatX > 0.5f);
            Input::SetGamepadButton(0, GamepadButton::DpadUp, hatY < -0.5f);
            Input::SetGamepadButton(0, GamepadButton::DpadDown, hatY > 0.5f);
            return 1;
        }
        const int32_t action = AMotionEvent_getAction(event);
        const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
        const size_t pointerIndex = static_cast<size_t>(
            (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        if (AMotionEvent_getPointerCount(event) > pointerIndex) {
            Input::SetMousePosition(AMotionEvent_getX(event, pointerIndex),
                                    AMotionEvent_getY(event, pointerIndex));
        }
        if (masked == AMOTION_EVENT_ACTION_DOWN ||
            masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            Input::SetMouseButton(MouseButton::Left, true);
        } else if (masked == AMOTION_EVENT_ACTION_UP ||
                   masked == AMOTION_EVENT_ACTION_POINTER_UP ||
                   masked == AMOTION_EVENT_ACTION_CANCEL) {
            Input::SetMouseButton(MouseButton::Left, false);
        }
        return 1;
    }
    return 0;
}

void AndroidWindow::PollEvents() {
    if (m_app == nullptr) return;
    int events = 0;
    android_poll_source* source = nullptr;
    while (ALooper_pollOnce(0, nullptr, &events,
                            reinterpret_cast<void**>(&source)) >= 0) {
        if (source != nullptr) source->process(m_app, source);
        if (m_app->destroyRequested) {
            m_shouldClose = true;
            break;
        }
        source = nullptr;
    }
}

} // namespace NaraGame

#endif
