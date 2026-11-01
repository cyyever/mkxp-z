//
//  systemImpl.cpp
//  Player
//
//  Created by ゾロアーク on 11/22/20.
//

#include "system.h"
#include "util/exception.h"
#include <SDL3/SDL_filesystem.h>
#include <memory>

#if defined(SDL_PLATFORM_WIN32)
#include <stdlib.h>
#include <windows.h>
#else
#include <locale>
#endif

#include <SDL3/SDL_loadso.h>
#include <cstring>
#include <string>

std::string systemImpl::getSystemLanguage() {
    static char buf[50] = {0};
#if defined(SDL_PLATFORM_WIN32)
    wchar_t wbuf[50] = {0};
    LANGID lid = GetUserDefaultLangID();
    LCIDToLocaleName(lid, wbuf, sizeof(wbuf), 0);
    wcstombs(buf, wbuf, sizeof(buf));
#else
    strncpy(buf, std::locale("").name().c_str(), sizeof(buf));
#endif
    
    for (int i = 0; (size_t)i < strlen(buf); i++) {
#ifdef SDL_PLATFORM_WIN32
        if (buf[i] == '-') {
            buf[i] = '_';
#else
        if (buf[i] == '.') {
            buf[i] = 0;
#endif
            break;
        }
    }
    return std::string(buf);
}

std::string systemImpl::getUserName() {
    
#ifdef SDL_PLATFORM_WIN32
    // The Ruby binding gets the username from the environment loaded
    // with Ruby instead, should fix getting it from WinAPI at some point
    return std::string("unused");
#else
    char ret[30];
    char *username = getenv("USER");
    if (username)
        strncpy(ret, username, sizeof(ret));
    return std::string(ret);
#endif
    
}
    
bool systemImpl::isWine() {
#if MKXPZ_PLATFORM != MKXPZ_PLATFORM_WINDOWS
    return false;
#else
    std::unique_ptr<SDL_SharedObject, decltype(&SDL_UnloadObject)> ntdll(
        SDL_LoadObject("ntdll.dll"), SDL_UnloadObject);
    return ntdll && SDL_LoadFunction(ntdll.get(), "wine_get_host_version") != nullptr;
#endif
}

bool systemImpl::isRosetta() {
    return false;
}

systemImpl::WineHostType systemImpl::getRealHostType() {
#if MKXPZ_PLATFORM == MKXPZ_PLATFORM_WINDOWS
    std::unique_ptr<SDL_SharedObject, decltype(&SDL_UnloadObject)> ntdll(
        SDL_LoadObject("ntdll.dll"), SDL_UnloadObject);
    if (!ntdll)
        return WineHostType::Windows;
    void (*wine_get_host_version)(const char **, const char **) =
    (void (*)(const char **, const char **))SDL_LoadFunction(ntdll.get(), "wine_get_host_version");
    
    if (wine_get_host_version == 0)
        return WineHostType::Windows;
    
    const char *kernel = 0;
    wine_get_host_version(&kernel, 0);

    if (!strcmp(kernel, "Darwin"))
        return WineHostType::Mac;
    if (!strcmp(kernel, "Linux"))
        return WineHostType::Linux;
    return WineHostType::Bsd;
#elif MKXPZ_PLATFORM == MKXPZ_PLATFORM_MACOS
    return WineHostType::Mac;
#elif MKXPZ_PLATFORM == MKXPZ_PLATFORM_LINUX
    return WineHostType::Linux;
#else
    return WineHostType::Bsd;
#endif
}

std::string systemImpl::getDefaultGameRoot() {
    const char *path = SDL_GetBasePath();
    if (!path)
        throw Exception(Exception::MKXPError, "Failed to retrieve executable path");
    return path;
}
