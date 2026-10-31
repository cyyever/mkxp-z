//
//  filesystemImplApple.mm
//  Player
//
//  Created by ゾロアーク on 11/21/20.
//

#import <AppKit/AppKit.h>
#import <SDL_syswm.h>

#import "filesystemImpl.h"

#define NSTOPATH(str) [NSFileManager.defaultManager fileSystemRepresentationWithPath:str]

std::string filesystemImpl::getDefaultGameRoot() {
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

std::string filesystemImpl::selectPath(SDL_Window *win, const char *msg, const char *prompt) {
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
