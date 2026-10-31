//
//  systemImplApple.m
//  Player
//
//  Created by ゾロアーク on 11/22/20.
//

#import <AppKit/AppKit.h>
#import <SDL_syswm.h>

#import <sys/sysctl.h>
#import "system.h"
#import "SettingsMenuController.h"

std::string systemImpl::getSystemLanguage() {
    @autoreleasepool {
        NSString *languageCode = NSLocale.currentLocale.languageCode;
        NSString *countryCode = NSLocale.currentLocale.countryCode;
        return std::string([NSString stringWithFormat:@"%@_%@", languageCode, countryCode].UTF8String);
    }
}

std::string systemImpl::getUserName() {
    @autoreleasepool {
        return std::string(NSUserName().UTF8String);
    }
}

int systemImpl::getScalingFactor() {
    return NSApplication.sharedApplication.mainWindow.backingScaleFactor;
}

bool systemImpl::isWine() {
    return false;
}

bool systemImpl::isRosetta() {
    int translated = 0;
    size_t size = sizeof(translated);
    int result = sysctlbyname("sysctl.proc_translated", &translated, &size, NULL, 0);

    if (result == -1)
        return false;

    return translated;
}

systemImpl::WineHostType systemImpl::getRealHostType() {
    return WineHostType::Mac;
}


// constant, if it's not nil then just raise the menu instead
SettingsMenu *smenu = nil;
void openSettingsWindow() {
    if (smenu == nil) {
        smenu = [SettingsMenu openWindow];
        return;
    }
    [smenu raise];
}

#define NSTOPATH(str) [NSFileManager.defaultManager fileSystemRepresentationWithPath:str]

std::string systemImpl::getDefaultGameRoot() {
    @autoreleasepool {
        NSString *defaultGameRoot = nil;
        if ([[NSBundle mainBundle] bundleIdentifier] == nil) {
            /* The executable isn't inside of a bundle; use the directory containing the executable as the default game root */
            defaultGameRoot = [[NSBundle mainBundle] bundlePath];
        } else {
            NSString *contentsGamePath = [[[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"Contents"] stringByAppendingPathComponent:@"Game"];
            BOOL isDir;
            if ([[NSFileManager defaultManager] fileExistsAtPath:contentsGamePath isDirectory:&isDir] && isDir) {
                /* The executable is inside of a bundle and Contents/Game exists inside the bundle; use Contents/Game in the bundle as the default game root */
                defaultGameRoot = contentsGamePath;
            } else {
                /* The executable is inside of a bundle and Contents/Game doesn't exist inside the bundle; use the directory that contains the bundle as the default game root */
                defaultGameRoot = [[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent];
            }
        }
        return std::string(NSTOPATH(defaultGameRoot));
    }
}

std::string systemImpl::selectPath(SDL_Window *win, const char *msg, const char *prompt) {
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        panel.canChooseDirectories = true;
        panel.canChooseFiles = false;

        if (msg) panel.message = @(msg);
        if (prompt) panel.prompt = @(prompt);
        //panel.directoryURL = [NSURL fileURLWithPath:NSFileManager.defaultManager.currentDirectoryPath];

        SDL_SysWMinfo windowinfo{};
        SDL_GetWindowWMInfo(win, &windowinfo);

        [panel beginSheetModalForWindow:windowinfo.info.cocoa.window completionHandler:^(NSModalResponse res){
            [NSApp stopModalWithCode:res];
        }];

        [NSApp runModalForWindow:windowinfo.info.cocoa.window];

        // The window needs to be brought to the front again after the OpenPanel closes
        [windowinfo.info.cocoa.window makeKeyAndOrderFront:nil];
        if (panel.URLs.count > 0)
            return std::string(NSTOPATH(panel.URLs[0].path));

        return std::string();
    }
}
