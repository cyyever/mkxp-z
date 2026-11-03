//
//  systemImpl.cpp
//  Player
//
//  Created by ゾロアーク on 11/22/20.
//

#include "system.h"
#include "util/exception.h"
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_stdinc.h>
#include <cstring>
#include <memory>
#include <string>

// The user's most preferred locale as "language_COUNTRY" (e.g. zh_CN), or
// just the language when no country is set; empty if unknown.
std::string systemImpl::getSystemLanguage() {
    int count = 0;
    std::unique_ptr<SDL_Locale *, decltype(&SDL_free)> locales(SDL_GetPreferredLocales(&count), SDL_free);
    if (!locales || count < 1 || !locales.get()[0] || !locales.get()[0]->language)
        return {};
    const SDL_Locale &locale = *locales.get()[0];
    std::string result = locale.language;
    if (locale.country && *locale.country) {
        result += '_';
        result += locale.country;
    }
    return result;
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

#ifndef SDL_PLATFORM_APPLE
// Apple builds implement these in systemImplApple.mm.
bool systemImpl::isRosetta() {
    return false;
}
#endif

systemImpl::WineHostType systemImpl::getRealHostType() {
#if MKXPZ_PLATFORM == MKXPZ_PLATFORM_WINDOWS
    std::unique_ptr<SDL_SharedObject, decltype(&SDL_UnloadObject)> ntdll(
        SDL_LoadObject("ntdll.dll"), SDL_UnloadObject);
    if (!ntdll)
        return WineHostType::Windows;
    void (*wine_get_host_version)(const char **, const char **) =
    (void (*)(const char **, const char **))SDL_LoadFunction(ntdll.get(), "wine_get_host_version");
    
    if (wine_get_host_version == nullptr)
        return WineHostType::Windows;
    
    const char *kernel = nullptr;
    wine_get_host_version(&kernel, nullptr);

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

#ifndef SDL_PLATFORM_APPLE
std::string systemImpl::getDefaultGameRoot() {
    const char *path = SDL_GetBasePath();
    if (!path)
        throw Exception(Exception::MKXPError, "Failed to retrieve executable path");
    return path;
}
#endif
