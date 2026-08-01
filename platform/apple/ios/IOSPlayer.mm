#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <NaraGame/core/Application.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGamePlayer/ProjectPlayer.h>

#include <thread>

namespace NaraGame {
void SetIOSMetalLayer(void* layer, uint32_t width, uint32_t height);
}

@interface NaraGameView : UIView
@end

@implementation NaraGameView
+ (Class)layerClass { return [CAMetalLayer class]; }
- (void)updateTouch:(NSSet<UITouch*>*)touches down:(BOOL)down {
    UITouch* touch = touches.anyObject;
    if (touch == nil) return;
    CGPoint point = [touch locationInView:self];
    const CGFloat scale = self.contentScaleFactor;
    NaraGame::Input::SetMousePosition(point.x * scale, point.y * scale);
    NaraGame::Input::SetMouseButton(NaraGame::MouseButton::Left, down);
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event; [self updateTouch:touches down:YES];
}
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event; [self updateTouch:touches down:YES];
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event; [self updateTouch:touches down:NO];
}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event; [self updateTouch:touches down:NO];
}
@end

namespace {
std::string StageBundledProject() {
    NSFileManager* files = NSFileManager.defaultManager;
    NSURL* caches = [files URLsForDirectory:NSCachesDirectory
                                  inDomains:NSUserDomainMask].firstObject;
    NSURL* root = [caches URLByAppendingPathComponent:@"NaraGameProject"];
    [files createDirectoryAtURL:root withIntermediateDirectories:YES
                     attributes:nil error:nil];
    for (NSString* name in @[@"assets.pak", @"ProjectSettings", @"Assets"]) {
        NSURL* destination = [root URLByAppendingPathComponent:name];
        [files removeItemAtURL:destination error:nil];
        NSURL* source = [NSBundle.mainBundle.resourceURL
                         URLByAppendingPathComponent:name];
        if ([files fileExistsAtPath:source.path])
            [files copyItemAtURL:source toURL:destination error:nil];
    }
    return root.path.UTF8String;
}
}

NaraGame::Application* NaraGame::CreateApplication(int, char**) {
    auto app = NaraGamePlayer::CreateProjectPlayer({StageBundledProject(), 0});
    return app.release();
}

@interface NaraGameAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation NaraGameAppDelegate
- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)options {
    (void)application; (void)options;
    UIScreen* screen = UIScreen.mainScreen;
    self.window = [[UIWindow alloc] initWithFrame:screen.bounds];
    UIViewController* controller = [UIViewController new];
    NaraGameView* view = [[NaraGameView alloc] initWithFrame:screen.bounds];
    view.contentScaleFactor = screen.scale;
    controller.view = view;
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];

    CAMetalLayer* layer = (CAMetalLayer*)view.layer;
    layer.contentsScale = screen.scale;
    layer.drawableSize = CGSizeMake(screen.bounds.size.width * screen.scale,
                                    screen.bounds.size.height * screen.scale);
    NaraGame::SetIOSMetalLayer((__bridge void*)layer,
        static_cast<uint32_t>(layer.drawableSize.width),
        static_cast<uint32_t>(layer.drawableSize.height));

    std::thread([] {
        NaraGame::Log::Init();
        NaraGame::Application* app = NaraGame::CreateApplication(0, nullptr);
        if (app != nullptr) {
            app->Run();
            delete app;
        }
        NaraGame::Log::Shutdown();
    }).detach();
    return YES;
}
@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil,
            NSStringFromClass([NaraGameAppDelegate class]));
    }
}
