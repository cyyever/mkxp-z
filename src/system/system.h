//
//  system.h
//  Player
//
//  Created by ゾロアーク on 11/22/20.
//

#ifndef system_h
#define system_h

#include <string>
#include <SDL3/SDL_platform_defines.h>

struct SDL_Window;

#define MKXPZ_PLATFORM_WINDOWS 0
#define MKXPZ_PLATFORM_MACOS 1
#define MKXPZ_PLATFORM_LINUX 2
#define MKXPZ_PLATFORM_BSD 3

#ifdef SDL_PLATFORM_WIN32
#define MKXPZ_PLATFORM MKXPZ_PLATFORM_WINDOWS
#elif defined SDL_PLATFORM_MACOS
#define MKXPZ_PLATFORM MKXPZ_PLATFORM_MACOS
#elif defined SDL_PLATFORM_LINUX
#define MKXPZ_PLATFORM MKXPZ_PLATFORM_LINUX
#elif defined __FreeBSD__
#define MKXPZ_PLATFORM MKXPZ_PLATFORM_BSD
#else
#error "Can't identify platform."
#endif

namespace systemImpl {
enum WineHostType {
    Windows,
    Linux,
    Mac,
    Bsd,
};
std::string getSystemLanguage();
std::string getUserName();
std::string getDefaultGameRoot();
#ifdef SDL_PLATFORM_APPLE
std::string selectPath(SDL_Window *win, const char *msg, const char *prompt);
#endif

bool isWine();
bool isRosetta();
WineHostType getRealHostType();
}

#ifdef SDL_PLATFORM_APPLE
void openSettingsWindow();
#endif

namespace mkxp_sys = systemImpl;

#endif /* system_h */
