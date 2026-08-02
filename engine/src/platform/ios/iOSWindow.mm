#include "platform/ios/iOSWindow.h"

#if defined(NARAGAME_PLATFORM_IOS)

#include "NaraGame/input/Input.h"
#import <GameController/GameController.h>

namespace NaraGame {
namespace {
void* g_metalLayer = nullptr;
uint32_t g_width = 0;
uint32_t g_height = 0;
}

void SetIOSMetalLayer(void* layer, uint32_t width, uint32_t height) {
    g_metalLayer = layer;
    g_width = width;
    g_height = height;
}

iOSWindow::iOSWindow(const WindowDesc& desc)
    : m_desc(desc), m_metalLayer(g_metalLayer),
      m_width(g_width), m_height(g_height) {
    if (m_width == 0) m_width = desc.width;
    if (m_height == 0) m_height = desc.height;
    if (m_metalLayer == nullptr) m_shouldClose = true;
}

void iOSWindow::PollEvents() {
    NSArray<GCController*>* controllers = [GCController controllers];
    for (int i = 0; i < 4; ++i) {
        if (i >= (int)controllers.count) {
            Input::SetGamepadConnected(i, false); continue;
        }
        GCExtendedGamepad* pad = controllers[(NSUInteger)i].extendedGamepad;
        if (pad == nil) { Input::SetGamepadConnected(i, false); continue; }
        Input::SetGamepadConnected(i, true);
        Input::SetGamepadButton(i, GamepadButton::A, pad.buttonA.isPressed);
        Input::SetGamepadButton(i, GamepadButton::B, pad.buttonB.isPressed);
        Input::SetGamepadButton(i, GamepadButton::X, pad.buttonX.isPressed);
        Input::SetGamepadButton(i, GamepadButton::Y, pad.buttonY.isPressed);
        Input::SetGamepadButton(i, GamepadButton::LeftShoulder, pad.leftShoulder.isPressed);
        Input::SetGamepadButton(i, GamepadButton::RightShoulder, pad.rightShoulder.isPressed);
        Input::SetGamepadButton(i, GamepadButton::LeftStick, pad.leftThumbstickButton.isPressed);
        Input::SetGamepadButton(i, GamepadButton::RightStick, pad.rightThumbstickButton.isPressed);
        Input::SetGamepadButton(i, GamepadButton::DpadUp, pad.dpad.up.isPressed);
        Input::SetGamepadButton(i, GamepadButton::DpadDown, pad.dpad.down.isPressed);
        Input::SetGamepadButton(i, GamepadButton::DpadLeft, pad.dpad.left.isPressed);
        Input::SetGamepadButton(i, GamepadButton::DpadRight, pad.dpad.right.isPressed);
        Input::SetGamepadAxis(i, GamepadAxis::LeftX, pad.leftThumbstick.xAxis.value);
        Input::SetGamepadAxis(i, GamepadAxis::LeftY, -pad.leftThumbstick.yAxis.value);
        Input::SetGamepadAxis(i, GamepadAxis::RightX, pad.rightThumbstick.xAxis.value);
        Input::SetGamepadAxis(i, GamepadAxis::RightY, -pad.rightThumbstick.yAxis.value);
        Input::SetGamepadAxis(i, GamepadAxis::LeftTrigger, pad.leftTrigger.value);
        Input::SetGamepadAxis(i, GamepadAxis::RightTrigger, pad.rightTrigger.value);
    }
}

} // namespace NaraGame

#endif
