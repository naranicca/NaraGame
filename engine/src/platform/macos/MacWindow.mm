#include "platform/macos/MacWindow.h"

#if defined(NARAGAME_PLATFORM_MACOS)

#include "NaraGame/input/Input.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

@interface NaraMetalView : NSView
@end

@implementation NaraMetalView
- (CALayer*)makeBackingLayer { return [CAMetalLayer layer]; }
- (BOOL)wantsUpdateLayer { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
@end

@interface NaraWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) NaraGame::MacWindow* owner;
@end

@implementation NaraWindowDelegate
- (BOOL)windowShouldClose:(id)sender {
    (void)sender;
    if (_owner != nullptr) _owner->RequestClose();
    return YES;
}
@end

namespace NaraGame {
namespace {

Key TranslateMacKey(unsigned short code) {
    switch (code) {
        case 0: return Key::A; case 1: return Key::S; case 2: return Key::D;
        case 3: return Key::F; case 4: return Key::H; case 5: return Key::G;
        case 6: return Key::Z; case 7: return Key::X; case 8: return Key::C;
        case 9: return Key::V; case 11: return Key::B; case 12: return Key::Q;
        case 13: return Key::W; case 14: return Key::E; case 15: return Key::R;
        case 16: return Key::Y; case 17: return Key::T; case 31: return Key::O;
        case 32: return Key::U; case 34: return Key::I; case 35: return Key::P;
        case 37: return Key::L; case 38: return Key::J; case 40: return Key::K;
        case 45: return Key::N; case 46: return Key::M;
        case 49: return Key::Space; case 36: return Key::Enter;
        case 53: return Key::Escape; case 48: return Key::Tab;
        case 51: return Key::Backspace; case 123: return Key::Left;
        case 124: return Key::Right; case 125: return Key::Down;
        case 126: return Key::Up; default: return Key::Unknown;
    }
}

} // namespace

MacWindow::MacWindow(const WindowDesc& desc)
    : m_desc(desc), m_width(desc.width), m_height(desc.height) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        const NSUInteger style = NSWindowStyleMaskTitled |
            NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
            (desc.resizable ? NSWindowStyleMaskResizable : 0);
        NSRect rect = NSMakeRect(0, 0, desc.width, desc.height);
        NSWindow* window = [[NSWindow alloc] initWithContentRect:rect
            styleMask:style backing:NSBackingStoreBuffered defer:NO];
        NSString* title = [NSString stringWithUTF8String:desc.title.c_str()];
        [window setTitle:title ?: @"NaraGame"];
        [window center];

        NaraMetalView* view = [[NaraMetalView alloc] initWithFrame:rect];
        [view setWantsLayer:YES];
        CAMetalLayer* layer = (CAMetalLayer*)view.layer;
        layer.contentsScale = window.backingScaleFactor;
        [window setContentView:view];
        [window makeFirstResponder:view];

        NaraWindowDelegate* delegate = [NaraWindowDelegate new];
        delegate.owner = this;
        window.delegate = delegate;
        [window makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];
        [app finishLaunching];

        m_window = (__bridge_retained void*)window;
        m_view = (__bridge_retained void*)view;
        m_delegate = (__bridge_retained void*)delegate;
        m_metalLayer = (__bridge void*)layer;
    }
}

MacWindow::~MacWindow() {
    @autoreleasepool {
        NSWindow* window = (__bridge_transfer NSWindow*)m_window;
        if (window != nil) [window close];
        (void)(__bridge_transfer NSView*)m_view;
        (void)(__bridge_transfer NaraWindowDelegate*)m_delegate;
        m_window = m_view = m_delegate = m_metalLayer = nullptr;
    }
}

void MacWindow::PollEvents() {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        NSEvent* event = nil;
        while ((event = [app nextEventMatchingMask:NSEventMaskAny
                    untilDate:[NSDate distantPast]
                    inMode:NSDefaultRunLoopMode dequeue:YES])) {
            const NSEventType type = event.type;
            if (type == NSEventTypeKeyDown || type == NSEventTypeKeyUp) {
                Input::SetKeyState(TranslateMacKey(event.keyCode),
                                   type == NSEventTypeKeyDown);
            } else if (type == NSEventTypeLeftMouseDown ||
                       type == NSEventTypeLeftMouseUp) {
                Input::SetMouseButton(MouseButton::Left,
                    type == NSEventTypeLeftMouseDown);
            } else if (type == NSEventTypeRightMouseDown ||
                       type == NSEventTypeRightMouseUp) {
                Input::SetMouseButton(MouseButton::Right,
                    type == NSEventTypeRightMouseDown);
            } else if (type == NSEventTypeMouseMoved ||
                       type == NSEventTypeLeftMouseDragged ||
                       type == NSEventTypeRightMouseDragged) {
                NSView* view = (__bridge NSView*)m_view;
                NSPoint point = [view convertPoint:event.locationInWindow
                                          fromView:nil];
                Input::SetMousePosition(point.x, view.bounds.size.height - point.y);
            } else if (type == NSEventTypeScrollWheel) {
                Input::AddMouseWheel(event.scrollingDeltaY);
            }
            [app sendEvent:event];
        }

        NSView* view = (__bridge NSView*)m_view;
        NSWindow* window = (__bridge NSWindow*)m_window;
        const NSSize size = view.bounds.size;
        const CGFloat scale = window.backingScaleFactor;
        m_width = static_cast<uint32_t>(size.width * scale);
        m_height = static_cast<uint32_t>(size.height * scale);
        CAMetalLayer* layer = (__bridge CAMetalLayer*)m_metalLayer;
        layer.contentsScale = scale;
        layer.drawableSize = CGSizeMake(m_width, m_height);
    }
}

} // namespace NaraGame

#endif
