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
};

} // namespace NaraGame
