#import <Foundation/Foundation.h>

#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGamePlayer/ProjectPlayer.h>

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
