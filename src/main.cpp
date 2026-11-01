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

#ifndef VK_NO_PROTOTYPES
#  define VK_NO_PROTOTYPES
#endif

#include "util/paths.h"
#include <SDL3/SDL_main.h>
#include <cstdlib>

#include "icon.png.xxd"

#include <alc.h>
#include <alext.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_sound/SDL_sound.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cassert>
#include <cstring>
#include <string>
#include <unistd.h>

#include "binding.h"
#include "sharedstate.h"
#include "eventthread.h"
#include "util/debugwriter.h"
#include "util/exception.h"
#include "display/gl/gl-debug.h"
#include "display/gl/gl-fun.h"

#include "filesystem/filesystem.h"

#include "system/system.h"

#if defined(SDL_PLATFORM_WIN32)
#include "resource.h"
#include <processenv.h>
#include <winsock2.h>
#include "util/win-consoleutils.h"

// Try to work around buggy GL drivers that tend to be in Optimus laptops
// by forcing MKXP to use the dedicated card instead of the integrated one
#include <windows.h>
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#ifdef SDL_PLATFORM_APPLE
#include <Availability.h>
#include "TouchBar.h"
#endif

#if !defined(__ANDROID__) && !defined(SDL_PLATFORM_APPLE) && !defined(_WIN32)
#  define MKXPZ_CHECK_FOR_WAYLAND_SUPPORT
#endif


static void rgssThreadError(RGSSThreadData *rtData, const std::string &msg);
static void showInitError(const std::string &msg);

static void mkxp_setenv(const char *key, const char *value) {
#ifdef _WIN32
  SetEnvironmentVariableA(key, value);
#else
  if (value != nullptr) {
    setenv(key, value, true);
  } else {
    unsetenv(key);
  }
#endif
}

static inline const char *glGetStringInt(GLenum name) {
  return (const char *)gl.GetString(name);
}

static void printGLInfo() {
  const std::string renderer(glGetStringInt(GL_RENDERER));
  const std::string version(glGetStringInt(GL_VERSION));

  Debug() << "GL Vendor    :" << glGetStringInt(GL_VENDOR);
  Debug() << "GL Renderer  :" << renderer;
  Debug() << "GL Version   :" << version;
  Debug() << "GLSL Version :" << glGetStringInt(GL_SHADING_LANGUAGE_VERSION);
}

static SDL_GLContext initGL(SDL_Window *win, Config &conf,
                            RGSSThreadData *threadData);

int rgssThreadFun(void *userdata) {
  RGSSThreadData *threadData = static_cast<RGSSThreadData *>(userdata);

  if (!SDL_GL_MakeCurrent(threadData->window, threadData->glContext)) {
    rgssThreadError(threadData, SDL_GetError());
    return 0;
  }

  /* Setup AL context */
  static const ALCint attrs[] = {
    /* HRTF is explicitly disabled here because it results in poor-quality audio
     * when enabled (see https://github.com/mkxp-z/mkxp-z/issues/341). By
     * default, it's enabled when OpenAL Soft detects that the user is using
     * headphones for audio drivers that support detecting if the user is using
     * headphones, and disabled regardless of whether or not the user is using
     * headphones if the audio driver does not support this detection. The HRTF
     * is required for positional audio support, so we'll need to find a way
     * around the audio quality issues and inconsistent detection of whether or
     * not the user is using headphones once we have positional audio support. */
    ALC_HRTF_SOFT, ALC_FALSE,
    0
  };
  ALCcontext *alcCtx = alcCreateContext(threadData->alcDev, attrs);

  if (!alcCtx) {
    rgssThreadError(threadData, "Error creating OpenAL context");
    return 0;
  }

  alcMakeContextCurrent(alcCtx);

  try {
    SharedState::initInstance(threadData);
  } catch (const Exception &exc) {
    showInitError(exc.msg);
    alcDestroyContext(alcCtx);

    return 0;
  }

  /* Start script execution */
  scriptBinding->execute();

  threadData->rqTermAck = true;
  threadData->ethread->requestTerminate();

  SharedState::finiInstance();

  alcDestroyContext(alcCtx);

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

static void setupWindowIcon(const Config &conf, SDL_Window *win) {
  SDL_IOStream *iconSrc;

  if (conf.iconPath.empty())
    iconSrc = SDL_IOFromConstMem(mkxp_assets_icon_png, mkxp_assets_icon_png_len);
  else
    iconSrc = SDL_IOFromFile(conf.iconPath.c_str(), "rb");

  SDL_Surface *iconImg = IMG_Load_IO(iconSrc, true);

  if (iconImg) {
    SDL_SetWindowIcon(win, iconImg);
    SDL_DestroySurface(iconImg);
  }
}

static SDL_Window *initVideo(const Config &conf) {
  SDL_WindowFlags winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;

  if (conf.winResizable)
    winFlags |= SDL_WINDOW_RESIZABLE;
  if (conf.fullscreen)
    winFlags |= SDL_WINDOW_FULLSCREEN;

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    return nullptr;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GetHintBoolean(SDL_HINT_OPENGL_ES_DRIVER, false) ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE);

  SDL_Window *window = SDL_CreateWindow(conf.windowTitle.c_str(), conf.defScreenW, conf.defScreenH, winFlags);
  if (window == nullptr) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
  return window;
}

int main(int argc, char *argv[]) {
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");

    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

    SDL_SetHint(
      SDL_HINT_OPENGL_ES_DRIVER,
      SDL_GetHintBoolean(
        SDL_HINT_OPENGL_ES_DRIVER,
#ifdef MKXPZ_USE_GLES_BY_DEFAULT
        true
#else
        false
#endif // MKXPZ_USE_GLES_BY_DEFAULT
      ) != false ? "1" : "0"
    );

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
        char buf[200];
        snprintf(buf, sizeof(buf), "Error allocating console: %lu",
                GetLastError());
        showInitError(std::string(buf));
      }
    }
#endif

    /* initialize SDL first */
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
      showInitError(std::string("Error initializing SDL: ") + SDL_GetError());
      return 0;
    }

    SDL_Window *win = initVideo(conf);
    if (win == nullptr) {
      showInitError(std::string("Error creating window: ") + SDL_GetError());
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

    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (IMG_Init(imgFlags) != imgFlags) {
      showInitError(std::string("Error initializing SDL_image: ") +
                    SDL_GetError());
      SDL_Quit();

      return 0;
    }

    if (TTF_Init() < 0) {
      showInitError(std::string("Error initializing SDL_ttf: ") +
                    SDL_GetError());
      IMG_Quit();
      SDL_Quit();

      return 0;
    }

    if (Sound_Init() == 0) {
      showInitError(std::string("Error initializing SDL_sound: ") +
                    Sound_GetError());
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();

      return 0;
    }
#if defined(SDL_PLATFORM_WIN32)
    WSAData wsadata = {0};
    if (WSAStartup(0x101, &wsadata) || wsadata.wVersion != 0x101) {
      char buf[200];
      snprintf(buf, sizeof(buf), "Error initializing winsock: %08X",
               WSAGetLastError());
      showInitError(
          std::string(buf)); // Not an error worth ending the program over
    }
#endif
    
#ifdef SDL_PLATFORM_APPLE
    {
        std::string downloadsPath = "/Users/" + mkxp_sys::getUserName() + "/Downloads";
        
        if (paths::toUtf8(std::filesystem::current_path().u8string()).find(downloadsPath) == 0) {
            showInitError(conf.game.title +
                          " cannot run from the Downloads directory.\n\n" +
                          "Please move the application to the Applications folder (or anywhere else) " +
                          "and try again.");
            return 0;
        }
    }
#endif
    
#ifdef SDL_PLATFORM_APPLE
#define DEBUG_FSELECT_MSG "Select the folder from which to load game files. This is the folder containing the game's INI."
#define DEBUG_FSELECT_PROMPT "Load Game"
    if (conf.manualFolderSelect) {
        std::string dataDirStr = mkxp_sys::selectPath(win, DEBUG_FSELECT_MSG, DEBUG_FSELECT_PROMPT);
        if (!dataDirStr.empty()) {
            conf.gameFolder = dataDirStr;
            std::error_code error;
            std::filesystem::current_path(paths::fromUtf8(dataDirStr), error);
            if (error) {
                showInitError("Unable to switch into game folder: " + dataDirStr);
                return 1;
            }
            Debug() << "Current directory set to" << dataDirStr;
            conf.read(argc, argv);
            conf.readGameINI();
        }
    }
#endif

    /* OSX and Windows have their own native ways of
     * dealing with icons; don't interfere with them */
#ifdef SDL_PLATFORM_LINUX
    setupWindowIcon(conf, win);
#else
    (void)setupWindowIcon;
#endif

    ALCdevice *alcDev = alcOpenDevice(0);

    if (!alcDev) {
      showInitError("Could not detect an available audio device.");
      SDL_DestroyWindow(win);
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();

      return 0;
    }

    const SDL_DisplayMode *displayMode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
    SDL_DisplayMode mode = displayMode ? *displayMode : SDL_DisplayMode{};

    /* Can't sync to display refresh rate if its value is unknown */
    if (!mode.refresh_rate)
      conf.syncToRefreshrate = false;

    EventThread eventThread;

    // SDL3 creates window contexts on the main thread. Hand the context over
    // before starting the RGSS renderer thread.
    SDL_GLContext glCtx = initGL(win, conf, nullptr);
    if (!glCtx)
        return 1;
    if (!SDL_GL_MakeCurrent(win, nullptr)) {
        showInitError(SDL_GetError());
        SDL_GL_DestroyContext(glCtx);
        return 1;
    }

    RGSSThreadData rtData(&eventThread, argv[0], win, alcDev, mode.refresh_rate,
                          mkxp_sys::getScalingFactor(), conf, glCtx);

    int winW, winH, drwW, drwH;
    SDL_GetWindowSize(win, &winW, &winH);
    rtData.windowSizeMsg.post(Vec2i(winW, winH));
    
    SDL_GetWindowSizeInPixels(win, &drwW, &drwH);
    rtData.drawableSizeMsg.post(Vec2i(drwW, drwH));

    /* Load and post key bindings */
    rtData.bindingUpdateMsg.post(loadBindings(conf));
    
#ifdef SDL_PLATFORM_APPLE
    // Create Touch Bar
    initTouchBar(win, conf);
#endif

    /* Start RGSS thread */
    SDL_Thread *rgssThread = SDL_CreateThread(rgssThreadFun, "rgss", &rtData);

    /* Start event processing */
    eventThread.process(rtData);

    /* Request RGSS thread to stop */
    rtData.rqTerm = true;

    /* Wait for RGSS thread response */
    for (int i = 0; i < 1000; ++i) {
      /* We can stop waiting when the request was ack'd */
      if (rtData.rqTermAck) {
        Debug() << "RGSS thread ack'd request after" << i * 10 << "ms";
        break;
      }

      /* Give RGSS thread some time to respond */
      SDL_Delay(10);
    }

    /* If RGSS thread ack'd request, wait for it to shutdown,
     * otherwise abandon hope and just end the process as is. */
    if (rtData.rqTermAck)
      SDL_WaitThread(rgssThread, 0);
    else
      SDL_ShowSimpleMessageBox(
          SDL_MESSAGEBOX_ERROR, conf.game.title.c_str(),
          std::string("The RGSS script seems to be stuck. "+conf.game.title+" will now force quit.").c_str(),
          win);

    if (!rtData.rgssErrorMsg.empty()) {
      Debug() << rtData.rgssErrorMsg;
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, conf.game.title.c_str(),
                               rtData.rgssErrorMsg.c_str(), win);
    }

    if (rtData.glContext)
      SDL_GL_DestroyContext(rtData.glContext);

    /* Clean up any remainin events */
    eventThread.cleanup();

    Debug() << "Shutting down.";

    alcCloseDevice(alcDev);
    SDL_DestroyWindow(win);

#if defined(SDL_PLATFORM_WIN32)
    if (wsadata.wVersion)
      WSACleanup();
#endif

    Sound_Quit();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();

    return 0;
}

static SDL_GLContext initGL(SDL_Window *win, Config &conf,
                            RGSSThreadData *threadData) {
  SDL_GLContext glCtx{};

  /* Setup GL context. Must be done in main thread since macOS 10.15 */
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    
  if (conf.debugMode)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

  glCtx = SDL_GL_CreateContext(win);

  if (!glCtx) {
    showInitError(std::string("Could not create OpenGL context: ") + SDL_GetError());
    return 0;
  }

  try {
    initGLFunctions();
  } catch (const Exception &exc) {
    showInitError(exc.msg);
    SDL_GL_DestroyContext(glCtx);

    return 0;
  }

  if (!conf.enableBlitting)
    gl.BlitFramebuffer = 0;

  gl.ClearColor(0, 0, 0, 1);
  gl.Clear(GL_COLOR_BUFFER_BIT);
  SDL_GL_SwapWindow(win);

  printGLInfo();

  bool vsync = conf.vsync || conf.syncToRefreshrate;
  SDL_GL_SetSwapInterval(vsync ? 1 : 0);

  // GLDebugLogger dLogger;
  return glCtx;
}
