/*
** main.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "util/paths.h"
#include <SDL3/SDL_main.h>
#include <cstdlib>
#include <format>
#include <exception>
#include <thread>

#include "icon.png.xxd"

#include "audio-util.h"

#include <SDL3/SDL.h>
#include <SDL3_sound/SDL_sound.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cassert>
#include <cstring>
#include <string>

#include "binding.h"
#include "sharedstate.h"
#include "eventthread.h"
#include "util/debugwriter.h"
#include "util/exception.h"
#include "display/gpu-device.h"
#include "display/gpu-frame-handoff.h"
#include "display/gpu-render-context.h"
#include "display/gpu-embedded.h"
#include "graphics.h"
#include <memory>
#include <cstdlib>

#include "filesystem/filesystem.h"

#include "system/system.h"

#if defined(SDL_PLATFORM_WIN32)
#include "resource.h"
#include "util/win-consoleutils.h"

#endif

#ifdef SDL_PLATFORM_APPLE
#include <Availability.h>
#include "TouchBar.h"
#endif


static void rgssThreadError(RGSSThreadData *rtData, const std::string &msg);
static void showInitError(const std::string &msg);

SDL_AudioDeviceID audioDevice = 0;

int rgssThreadFun(void *userdata) {
    auto *threadData = static_cast<RGSSThreadData *>(userdata);
    struct Completion {
        std::atomic<bool> &exited;
        ~Completion() { exited.store(true, std::memory_order_release); }
    } completion{threadData->rgssExited};
    try {
        auto pipelines = gpu::createEnginePipelines(threadData->gpuDevice->native());
        gpu::RenderContext renderer(*threadData->gpuDevice, *pipelines,
                                   threadData->config.defScreenW, threadData->config.defScreenH);
        gpu::RenderContext::install(&renderer);
        renderer.setMipmapsEnabled(threadData->config.smoothScalingMipmaps);
        bool initialized = false;
        try {
            SharedState::initInstance(threadData);
            initialized = true;
            renderer.setProjectionResolver([&renderer](bool scalingAllowed) {
                if (scalingAllowed && shState->config().enableHires && shState->graphics().isPingPongFramebufferActive())
                    return std::array<int,2>{shState->graphics().width(), shState->graphics().height()};
                const auto viewport = renderer.viewport();
                return std::array<int,2>{viewport.w, viewport.h};
            });
            scriptBinding->execute();
            renderer.flush();
        } catch (...) {
            if (initialized) SharedState::finiInstance();
            throw;
        }
        SharedState::finiInstance();
    } catch (const Exception &error) {
        rgssThreadError(threadData, error.msg);
        return 1;
    } catch (const std::exception &error) {
        rgssThreadError(threadData, error.what());
        return 1;
    }
    threadData->rqTermAck = true;
    threadData->ethread->requestTerminate();
    return 0;
}

static void rgssThreadError(RGSSThreadData *rtData, const std::string &msg) {
  rtData->rgssErrorMsg = msg;
  rtData->ethread->requestTerminate();
  rtData->rqTermAck = true;
}

static void showInitError(const std::string &msg) {
  Debug() << msg;
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "mkxp-z", msg.c_str(), 0);
}

[[maybe_unused]] static void setupWindowIcon(const Config &conf, SDL_Window *win) {
  SDL_IOStream *iconSrc;

  if (conf.iconPath.empty())
    iconSrc = SDL_IOFromConstMem(mkxp_assets_icon_png, mkxp_assets_icon_png_len);
  else
    iconSrc = SDL_IOFromFile(conf.iconPath.c_str(), "rb");

  SDL_Surface *iconImg = SDL_LoadPNG_IO(iconSrc, true);

  if (iconImg) {
    SDL_SetWindowIcon(win, iconImg);
    SDL_DestroySurface(iconImg);
  }
}

static SDL_Window *initVideo(const Config &conf) {
  SDL_WindowFlags winFlags = SDL_WINDOW_HIGH_PIXEL_DENSITY;

  if (conf.winResizable)
    winFlags |= SDL_WINDOW_RESIZABLE;
  if (conf.fullscreen)
    winFlags |= SDL_WINDOW_FULLSCREEN;

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    return nullptr;
  }


  SDL_Window *window = SDL_CreateWindow(conf.windowTitle.c_str(), conf.defScreenW, conf.defScreenH, winFlags);
  if (window == nullptr) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
  return window;
}

int main(int argc, char *argv[]) {
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");




#ifndef WORKDIR_CURRENT
    std::string dataDir;
#if defined(__linux__)
    if (const char *sourceDir = std::getenv("SRCDIR"))
        dataDir = sourceDir;
#endif
    if (dataDir.empty())
        dataDir = mkxp_sys::getDefaultGameRoot();
    std::error_code directoryError;
    std::filesystem::current_path(paths::fromUtf8(dataDir), directoryError);
    if (directoryError) {
        showInitError("Unable to switch into game folder: " + dataDir);
        return 1;
    }
#endif

    /* now we load the config */
    Config conf;
    conf.read(argc, argv);
    if (conf.windowTitle.empty())
      conf.windowTitle = conf.game.title;

#if defined(SDL_PLATFORM_WIN32)
    // Create a debug console in debug mode
    if (conf.winConsole) {
      if (setupWindowsConsole()) {
        reopenWindowsStreams();
      } else {
        showInitError(std::format("Console initialization failed: {}", GetLastError()));
      }
    }
#endif

    /* initialize SDL first */
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
      showInitError(std::format("SDL initialization failed: {}", SDL_GetError()));
      return 0;
    }

    SDL_Window *win = initVideo(conf);
    if (win == nullptr) {
      showInitError(std::format("Window creation failed: {}", SDL_GetError()));
      return 0;
    }

    if (!EventThread::allocUserEvents()) {
      showInitError("Error allocating SDL user events");
      return 0;
    }

    if (conf.rgssVersion != 1) {
      showInitError("Only RGSS1 (RPG Maker XP) games are supported.");
      SDL_Quit();
      return 0;
    }

    if (!TTF_Init()) {
      showInitError(std::format("Font initialization failed: {}", SDL_GetError()));
      SDL_Quit();

      return 0;
    }

    if (Sound_Init() == 0) {
      showInitError(std::format("Audio decoder initialization failed: {}", Sound_GetError()));
      TTF_Quit();
      SDL_Quit();

      return 0;
    }
    
#ifdef SDL_PLATFORM_APPLE
    {
        const char *downloadsPath = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
        
        if (!downloadsPath) {
            showInitError(std::format("Downloads folder lookup failed: {}", SDL_GetError()));
            return 0;
        }
        if ((paths::toUtf8(std::filesystem::current_path().u8string()) + "/").starts_with(downloadsPath)) {
            showInitError("Move the game out of Downloads, then launch it again.");
            return 0;
        }
    }
#endif
    
#ifdef SDL_PLATFORM_APPLE
#define DEBUG_FSELECT_MSG "Select the folder from which to load game files. This is the folder containing the game's INI."
#define DEBUG_FSELECT_PROMPT "Load Game"
    if (conf.manualFolderSelect) {
        std::string dataDirStr;
        try {
            dataDirStr = mkxp_sys::selectPath(win, DEBUG_FSELECT_MSG, DEBUG_FSELECT_PROMPT);
        } catch (const std::exception &error) {
            showInitError(error.what());
            return 1;
        }
        if (!dataDirStr.empty()) {
            conf.gameFolder = dataDirStr;
            std::error_code error;
            std::filesystem::current_path(paths::fromUtf8(dataDirStr), error);
            if (error) {
                showInitError("Unable to switch into game folder: " + dataDirStr);
                return 1;
            }
            conf.read(argc, argv);
            conf.readGameINI();
        }
    }
#endif

    /* OSX and Windows have their own native ways of
     * dealing with icons; don't interfere with them */
#ifdef SDL_PLATFORM_LINUX
    setupWindowIcon(conf, win);
#endif

    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
      audioDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);

    if (!audioDevice) {
      showInitError("Could not detect an available audio device.");
      SDL_DestroyWindow(win);
      TTF_Quit();
      SDL_Quit();

      return 0;
    }

    const SDL_DisplayMode *displayMode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
    SDL_DisplayMode mode = displayMode ? *displayMode : SDL_DisplayMode{};

    /* Can't sync to display refresh rate if its value is unknown */
    if (!mode.refresh_rate)
      conf.syncToRefreshrate = false;

    EventThread eventThread;

    std::unique_ptr<gpu::Device> gpuDevice;
    try {
        gpuDevice = std::make_unique<gpu::Device>(win, gpu::engineShaderFormats(), conf.debugMode);
        const auto mode = conf.vsync || conf.syncToRefreshrate ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (SDL_WindowSupportsGPUPresentMode(gpuDevice->native(), win, mode) &&
            !SDL_SetGPUSwapchainParameters(gpuDevice->native(), win, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
            throw std::runtime_error(SDL_GetError());
    } catch (const std::exception &error) {
        showInitError(error.what());
        gpuDevice.reset();
        SDL_CloseAudioDevice(audioDevice);
        SDL_DestroyWindow(win);
        Sound_Quit(); TTF_Quit(); SDL_Quit();
        return 1;
    }
    gpu::FrameHandoff gpuPresentation;
    RGSSThreadData rtData(&eventThread, argv[0], win, mode.refresh_rate,
                          conf, gpuDevice.get(), &gpuPresentation);

    int winW{}, winH{}, drwW{}, drwH{};
    SDL_GetWindowSize(win, &winW, &winH);
    
    SDL_GetWindowSizeInPixels(win, &drwW, &drwH);
    rtData.windowGeometryMsg.post({Vec2i(winW, winH), Vec2i(drwW, drwH)});

    /* Load and post key bindings */
    rtData.bindingUpdateMsg.post(loadBindings(conf));
    
#ifdef SDL_PLATFORM_APPLE
    // Create Touch Bar
    initTouchBar(win, conf);
#endif

    /* Start RGSS thread */
    std::jthread rgssThread(rgssThreadFun, &rtData);

    /* Always request/join worker shutdown even if main-thread event handling
     * throws. Keep this error separate from the worker-owned error string. */
    std::string eventThreadError;
    try {
        eventThread.process(rtData);
    } catch (const Exception &error) {
        eventThreadError = error.msg;
    } catch (const std::exception &error) {
        eventThreadError = error.what();
    }

    /* Request RGSS thread to stop */
    rtData.rqTerm = true;
    gpuPresentation.close();

    /* Wait for actual worker completion, not its earlier termination ack. */
    for (int i = 0; i < 1000; ++i) {
      if (rtData.rgssExited.load(std::memory_order_acquire))
        break;

      /* Give RGSS thread some time to respond */
      SDL_Delay(10);
    }

    if (rtData.rgssExited.load(std::memory_order_acquire)) {
      rgssThread.join();
    } else {
      // A detached worker would keep references to rtData, the window and GPU
      // resources. End the process without running destructors under that worker.
      SDL_ShowSimpleMessageBox(
          SDL_MESSAGEBOX_ERROR, conf.game.title.c_str(),
          "Game script stopped responding. Closing game.", win);
      std::_Exit(EXIT_FAILURE);
    }

    if (!eventThreadError.empty()) {
      Debug() << eventThreadError;
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, conf.game.title.c_str(),
                               eventThreadError.c_str(), win);
    }

    if (!rtData.rgssErrorMsg.empty()) {
      Debug() << rtData.rgssErrorMsg;
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, conf.game.title.c_str(),
                               rtData.rgssErrorMsg.c_str(), win);
    }

    gpuDevice.reset();

    /* Clean up any remainin events */
    eventThread.cleanup();

    SDL_CloseAudioDevice(audioDevice);
    SDL_DestroyWindow(win);


    Sound_Quit();
    TTF_Quit();
    SDL_Quit();

    return 0;
}
