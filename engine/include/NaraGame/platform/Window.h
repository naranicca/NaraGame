#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace NaraGame {

struct WindowDesc {
    std::string title = "NaraGame";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool resizable = true;
};

// Raw native message hook. Editor / tools install one to intercept the
// platform's native window messages -- Dear ImGui's Win32 backend needs
// to see WM_* events before the engine consumes them. Returning true
// means "consumed; skip the default engine handler". `nativeHandle` is
// the same value as Window::NativeHandle() (HWND / NSWindow* / ...);
// `msg`, `wparam`, `lparam` come straight from the platform call (cast
// to the right width on receive). Backends that don't speak this model
// (mobile, future GLFW path) can ignore the hook safely -- the default
// SetMessageHook is a no-op.
using WindowMessageHook = bool(*)(void* nativeHandle, unsigned int msg,
                                  unsigned long long wparam,
                                  long long lparam);

// Abstract OS window. The concrete type is chosen per platform by Create().
class Window {
public:
    static std::unique_ptr<Window> Create(const WindowDesc& desc);

    virtual ~Window() = default;

    virtual void PollEvents() = 0;
    virtual bool ShouldClose() const = 0;
    virtual void RequestClose() = 0;

    virtual uint32_t Width() const = 0;
    virtual uint32_t Height() const = 0;

    // Native handle for the RHI backend (HWND, NSWindow*, ANativeWindow*, ...).
    virtual void* NativeHandle() const = 0;

    virtual const WindowDesc& Desc() const = 0;

    // Install a hook to receive native window messages first. Pass
    // nullptr to clear. Default no-op so backends that don't surface
    // a message pump still compile.
    virtual void SetMessageHook(WindowMessageHook /*hook*/) {}
};

} // namespace NaraGame
