//
//  TouchBar.mm
//  mkxp-z
//
//  Created by ゾロア on 1/14/22.
//

#import <AppKit/AppKit.h>
#import <SDL3/SDL.h>
#import <SDL3/SDL_events.h>
#import <SDL3/SDL_timer.h>
#import <SDL3/SDL_scancode.h>

#import "TouchBar.h"
#import "sharedstate.h"
#import "eventthread.h"

#import "display/graphics.h"
#import "config.h"

#include "gear.png.xxd"
#include "gobackward.png.xxd"

MKXPZTouchBar *_sharedTouchBar;

@interface MKXPZTouchBar () {
    NSWindow *_parent;
    NSTextField *fpsLabel;
    NSSegmentedControl *functionKeys;
}

@property (retain,nonatomic) NSString *gameTitle;

-(void)updateFPSDisplay:(uint32_t)value;
-(void)setBarLayoutWithSettingsButton:(bool)showSettings andResetButton:(bool)showReset;
@end

@implementation MKXPZTouchBar

@synthesize gameTitle;

+(MKXPZTouchBar*)sharedTouchBar {
    if (!_sharedTouchBar)
        _sharedTouchBar = [MKXPZTouchBar new];
    return _sharedTouchBar;
}

-(void)setBarLayoutWithSettingsButton:(bool)showSettings andResetButton:(bool)showReset {
    if (!functionKeys) {
        functionKeys = [NSSegmentedControl segmentedControlWithLabels:@[@"F5", @"F6", @"F7", @"F8", @"F9"] trackingMode:NSSegmentSwitchTrackingMomentary target:self action:@selector(simFunctionKey)];
        functionKeys.segmentStyle = NSSegmentStyleSeparated;
    }
    if (!fpsLabel) {
        fpsLabel = [NSTextField labelWithString:@"Loading..."];
        fpsLabel.alignment = (showReset || showSettings) ? NSTextAlignmentCenter : NSTextAlignmentRight;
        fpsLabel.font = [NSFont labelFontOfSize:NSFont.smallSystemFontSize];
    }
    
    NSMutableArray *items = [NSMutableArray arrayWithArray:@[@"function", NSTouchBarItemIdentifierFlexibleSpace]];
    
    if (showReset || showSettings) {
        [items addObject:@"icon"];
        [items addObject:@"fps"];
        [items addObject:NSTouchBarItemIdentifierFlexibleSpace];
        if (showSettings) [items addObject:@"rebind"];
        if (showReset) [items addObject:@"reset"];
    } else {
        [items addObject:@"fps"];
        [items addObject:@"icon"];
    }
    self.defaultItemIdentifiers = items;
}

-(instancetype)init {
    self = [super init];
    self.delegate = self;
    
    return self;
}

-(NSWindow*)parent {
    return _parent;
}

-(NSWindow*)setParent:(NSWindow*)window {
    _parent = window;
    return _parent;
}

-(NSTouchBarItem *)touchBar:(NSTouchBar *)touchBar makeItemForIdentifier:(NSTouchBarItemIdentifier)identifier {
    NSCustomTouchBarItem *ret = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
    if ([identifier isEqualToString:@"reset"]) {
        NSData *data = [NSData dataWithBytesNoCopy:(void *)mkxp_macos_gobackward_png length:mkxp_macos_gobackward_png_len freeWhenDone:false];
        ret.view = [NSButton buttonWithImage:[[NSImage alloc] initWithData:data] target:self action:@selector(simF12)];
        
        ((NSButton*)ret.view).bezelColor = [NSColor colorWithRed:0xac/255.0 green:0x14/255.0 blue:0x01/255.0 alpha:1.0];
    }
    else if ([identifier isEqualToString:@"rebind"]) {
        NSData *data = [NSData dataWithBytesNoCopy:(void *)mkxp_macos_gear_png length:mkxp_macos_gear_png_len freeWhenDone:false];
        ret.view = [NSButton buttonWithImage:[[NSImage alloc] initWithData:data] target:self action:@selector(openSettingsMenu)];
    }
    else if ([identifier isEqualToString:@"icon"]) {
        NSImage *appIcon = [[NSApplication sharedApplication] applicationIconImage];
        appIcon.size = {30, 30};
        ret.view = [NSImageView imageViewWithImage:appIcon];
        
    }
    else if ([identifier isEqualToString:@"fps"]) {
        ret.view = fpsLabel;
    }
    else if ([identifier isEqualToString:@"function"]) {
        ret.view = functionKeys;
    }
    else {
        // hopefully this should get the compiler to free the touchbaritem here
        ret = nil;
        return nil;
    }
    return ret;
}

-(void)updateFPSDisplay:(uint32_t)value {
    if (fpsLabel) {
        int targetFrameRate = shState->graphics().getFrameRate();
        dispatch_async(dispatch_get_main_queue(), ^{
            @autoreleasepool {
                self->fpsLabel.stringValue = [NSString stringWithFormat:@"%@\n%i FPS (%i%%)", self.gameTitle, value, (int)((float)value / (float)targetFrameRate * 100)];
            }
        });
    }
}

-(void)simF12 {
    [self simulateKeypress:SDL_SCANCODE_F12];
}

-(void)simFunctionKey {
    [self simulateKeypress:(SDL_Scancode)(SDL_SCANCODE_F5 + functionKeys.selectedSegment)];
}

-(void)simulateKeypress:(SDL_Scancode)scancode {
    [self simulateKeyDown:scancode];
    double afr = shState->graphics().averageFrameRate();
    afr = (afr >= 1) ? afr : 1;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1000000 * afr), dispatch_get_main_queue(), ^{
        [self simulateKeyUp:scancode];
    });
}

-(void)simulateKeyDown:(SDL_Scancode)scancode {
    SDL_Event e{};
    e.key.type = SDL_EVENT_KEY_DOWN;
    e.key.down = true;
    e.key.timestamp = SDL_GetTicksNS();
    e.key.scancode = scancode;
    e.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
    e.key.windowID = SDL_GetWindowID(shState->sdlWindow());
    SDL_PushEvent(&e);
}

-(void)simulateKeyUp:(SDL_Scancode)scancode {
    SDL_Event e{};
    e.key.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    e.key.timestamp = SDL_GetTicksNS();
    e.key.scancode = scancode;
    e.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
    e.key.windowID = SDL_GetWindowID(shState->sdlWindow());
    SDL_PushEvent(&e);
}

-(void)openSettingsMenu {
    shState->eThread().requestSettingsMenu();
}

@end

void initTouchBar(SDL_Window *win, Config &conf) {
    NSWindow *nativeWindow = (__bridge NSWindow *)SDL_GetPointerProperty(
            SDL_GetWindowProperties(win), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    
    MKXPZTouchBar *tb = MKXPZTouchBar.sharedTouchBar;
    nativeWindow.touchBar = tb;
    tb.parent = nativeWindow;
    tb.gameTitle = @(conf.game.title.c_str());
    [tb setBarLayoutWithSettingsButton:conf.enableSettings andResetButton:conf.enableReset];
}

void updateTouchBarFPSDisplay(uint32_t value) {
    [MKXPZTouchBar.sharedTouchBar updateFPSDisplay:value];
}
