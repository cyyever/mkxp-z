/*
 ** eventthread.cpp
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

#include "eventthread.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_touch.h>
#include <SDL3/SDL_rect.h>

#include <cmath>
#include <format>
#include <memory>

#include "sharedstate.h"
#include "graphics.h"
#include "display/gpu-frame-handoff.h"

#ifndef SDL_PLATFORM_APPLE
#include "settingsmenu.h"
#else
#include "system/system.h"
#include "filesystem/filesystem.h"
#include "TouchBar.h"
#endif

#include "gamecontrollerdb.txt.xxd"

#include "debugwriter.h"

#include <string.h>


uint8_t EventThread::keyStates[];
EventThread::ControllerState EventThread::controllerState;
EventThread::MouseState EventThread::mouseState;
EventThread::TouchState EventThread::touchState;
std::atomic<int> EventThread::verticalScrollDistance;

/* User event codes */
enum
{
    REQUEST_SETFULLSCREEN = 0,
    REQUEST_WINRESIZE,
    REQUEST_WINRENAME,
    REQUEST_WINCENTER,
    REQUEST_MESSAGEBOX,
    REQUEST_SETCURSORVISIBLE,
    
    REQUEST_TEXTMODE,
    
    REQUEST_SETTINGS,
    
    UPDATE_FPS,
    UPDATE_SCREEN_RECT,
    REQUEST_GPU_PRESENT,
    
    EVENT_COUNT
};

static uint32_t usrIdStart;

bool EventThread::allocUserEvents()
{
    usrIdStart = SDL_RegisterEvents(EVENT_COUNT);
    
    if (usrIdStart == 0)
        return false;
    
    return true;
}

EventThread::EventThread()
: ctrl(0),
fullscreen(false),
showCursor(false)
{
}

SDL_TimerID hideCursorTimerID = 0;
Uint32 cursorTimerCallback(void *param, SDL_TimerID, Uint32 interval)
{
	EventThread *ethread = static_cast<EventThread*>(param);
	hideCursorTimerID = 0;
	ethread->requestShowCursor(ethread->getShowCursor());
	return 0;
}
void EventThread::cursorTimer()
{
	SDL_RemoveTimer(hideCursorTimerID);
	hideCursorTimerID = SDL_AddTimer(500, cursorTimerCallback, this);
}

void EventThread::process(RGSSThreadData &rtData)
{
    SDL_Event event;
    SDL_Window *win = rtData.window;
    auto &windowGeometryMsg = rtData.windowGeometryMsg;
    
    
    fullscreen = rtData.config.fullscreen;
    int toggleFSMod = rtData.config.anyAltToggleFS ? SDL_KMOD_ALT : SDL_KMOD_LALT;
    
    bool displayingFPS = rtData.config.displayFPS;
    
    if (displayingFPS || rtData.config.printFPS)
        fps.sendUpdates = true;

    bool cursorInWindow = false;
    /* Will be updated eventually */
    SDL_Rect gameScreen = { 0, 0, 0, 0 };
    
    /* SDL doesn't send an initial FOCUS_GAINED event */
    bool windowFocused = true;
    
    bool terminate = false;
    
    SDL_AddGamepadMappingsFromIO(
        SDL_IOFromConstMem(mkxp_assets_gamecontrollerdb_txt, mkxp_assets_gamecontrollerdb_txt_len),
    1);
    
    int gamepadCount = 0;
    SDL_JoystickID *gamepads = SDL_GetGamepads(&gamepadCount);
    if (gamepadCount > 0)
        ctrl = SDL_OpenGamepad(gamepads[0]);
    SDL_free(gamepads);

    std::string title;
    
    std::string pendingTitle;
    bool havePendingTitle = false;
    
    bool resetting = false;
    
    int winW, winH;
    int i, rc;
    

    
    SDL_GetWindowSize(win, &winW, &winH);
    
    // Just in case it's started when the window is opened
    // for some dumb reason
    SDL_StopTextInput(win);
    
    textInputBuffer.clear();
#ifndef SDL_PLATFORM_APPLE
    std::unique_ptr<SettingsMenu> sMenu;
#else
    // Will always be 0
    void *sMenu = 0;
#endif
    
    // Only the SDL window thread acquires/presents swapchain textures. Keep
    // the last completed snapshot here for expose events and modal repaint.
    std::optional<gpu::CompletedFrame> lastGpuFrame;
    auto presentGpuFrame = [&]() {
        try {
            if (!rtData.gpuDevice || !rtData.gpuPresentation) return true;
            if (auto next = rtData.gpuPresentation->tryTake()) {
                next->ready.wait();
                lastGpuFrame = std::move(next);
            }
            if (!lastGpuFrame) return true;
            gpu::CommandBuffer command(rtData.gpuDevice->native());
            Uint32 width = 0, height = 0;
            auto *swapchain = rtData.gpuDevice->acquireFrame(command, width, height);
            if (swapchain) {
                SDL_GPUBlitInfo blit{};
                blit.source.texture = lastGpuFrame->texture.native();
                blit.source.w = lastGpuFrame->texture.width();
                blit.source.h = lastGpuFrame->texture.height();
                blit.destination.texture = swapchain;
                blit.destination.w = width;
                blit.destination.h = height;
                blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                blit.filter = SDL_GPU_FILTER_NEAREST;
                rtData.gpuDevice->blit(command, blit);
            }
            command.submit();
            return true;
        } catch (const std::exception &error) {
            Debug() << "GPU presentation failed:" << error.what();
            rtData.rqTerm = true;
            rtData.gpuPresentation->close();
            return false;
        }
    };

    while (true)
    {
        if (!SDL_WaitEvent(&event))
        {
            Debug() << "EventThread: Event error";
            break;
        }
        if (event.type == usrIdStart + REQUEST_GPU_PRESENT) {
            gpuPresentQueued = false;
            if (!presentGpuFrame()) break;
            continue;
        }
        if (event.type == SDL_EVENT_WINDOW_EXPOSED && event.window.windowID == SDL_GetWindowID(win)) {
            if (!presentGpuFrame()) break;
        }
#ifndef SDL_PLATFORM_APPLE
        if (sMenu && sMenu->onEvent(event))
        {
            if (sMenu->destroyReq())
            {
                sMenu.reset();
                
                updateCursorState(cursorInWindow && windowFocused, gameScreen);
            }
            
            continue;
        }
#endif
        
        /* Preselect and discard unwanted events here */
        switch (event.type)
        {
            case SDL_EVENT_MOUSE_BUTTON_DOWN :
            case SDL_EVENT_MOUSE_BUTTON_UP :
            case SDL_EVENT_MOUSE_MOTION :
                if (event.button.which == SDL_TOUCH_MOUSEID)
                    continue;
                break;
                
            case SDL_EVENT_FINGER_DOWN :
            case SDL_EVENT_FINGER_UP :
            case SDL_EVENT_FINGER_MOTION :
                if (event.tfinger.fingerID >= MAX_FINGERS)
                    continue;
                break;
        }
        
        /* Now process the rest */
        switch (event.type)
        {
                    case SDL_EVENT_WINDOW_RESIZED :
                    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED :
                    {
                        // PIXEL_SIZE_CHANGED payloads are not logical window units.
                        SDL_GetWindowSize(win, &winW, &winH);
                        int drwW, drwH;
                        SDL_GetWindowSizeInPixels(win, &drwW, &drwH);
                        
                        windowGeometryMsg.post({Vec2i(winW, winH), Vec2i(drwW, drwH)});
                        resetInputStates();
                        break;
                        
                    }
                    case SDL_EVENT_WINDOW_MOUSE_ENTER :
                        cursorInWindow = true;
                        mouseState.inWindow = true;
                        updateCursorState(cursorInWindow && windowFocused && !sMenu, gameScreen);
                        
                        break;
                        
                    case SDL_EVENT_WINDOW_MOUSE_LEAVE :
                        cursorInWindow = false;
                        mouseState.inWindow = false;
                        updateCursorState(cursorInWindow && windowFocused && !sMenu, gameScreen);
                        
                        break;
                        
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED :
                        terminate = true;
                        
                        break;
                        
                    case SDL_EVENT_WINDOW_FOCUS_GAINED :
                        windowFocused = true;
                        updateCursorState(cursorInWindow && windowFocused && !sMenu, gameScreen);
                        
                        break;
                        
                    case SDL_EVENT_WINDOW_FOCUS_LOST :
                        windowFocused = false;
                        updateCursorState(cursorInWindow && windowFocused && !sMenu, gameScreen);
                        resetInputStates();
                        
                        break;
            case SDL_EVENT_TEXT_INPUT :
                {
                    auto guard = lockText();
                    if (textInputBuffer.size() < 512) {
                        textInputBuffer += event.text.text;
                    }
                }
                break;
                
            case SDL_EVENT_QUIT :
                terminate = true;
                Debug() << "EventThread termination requested";
                
                break;
                
            case SDL_EVENT_KEY_DOWN :
                if (event.key.scancode == SDL_SCANCODE_RETURN &&
                    (event.key.mod & toggleFSMod))
                {
                    setFullscreen(win, !fullscreen);
                    if (!fullscreen && havePendingTitle)
                    {
                        SDL_SetWindowTitle(win, pendingTitle.c_str());
                        pendingTitle.clear();
                        havePendingTitle = false;
                    }
                    
                    break;
                }
                
                if (event.key.scancode == SDL_SCANCODE_F1 && rtData.config.enableSettings)
                {
                    // Do not open settings menu until initializing shared state.
                    // Opening before initializing shared state will crash (segmentation fault).
                    if (!shState)
                    {
                        break;
                    }

#ifndef SDL_PLATFORM_APPLE
                    if (!sMenu)
                    {
                        sMenu = std::make_unique<SettingsMenu>(rtData);
                        updateCursorState(false, gameScreen);
                    }
                    
                    sMenu->raise();
#else
                    openSettingsWindow();
#endif
                }
                
                if (event.key.scancode == SDL_SCANCODE_F2)
                {
                    if (!displayingFPS)
                    {
                        
                        fps.sendUpdates = true;
                        displayingFPS = true;
                    }
                    else
                    {
                        displayingFPS = false;
                        
                        if (!rtData.config.printFPS)
                            fps.sendUpdates = false;
                        
                        if (fullscreen)
                        {
                            /* Prevent fullscreen flicker */
                            pendingTitle = rtData.config.windowTitle;
                            havePendingTitle = true;
                            
                            break;
                        }
                        
                        SDL_SetWindowTitle(win, rtData.config.windowTitle.c_str());
                    }
                    
                    break;
                }
                
                if (event.key.scancode == SDL_SCANCODE_F12)
                {
                    if (!rtData.config.enableReset)
                        break;
                    
                    if (resetting)
                        break;
                    
                    resetting = true;
                    rtData.rqResetFinish = false;
                    rtData.rqReset = true;
                    break;
                }
                
                keyStates[event.key.scancode] = true;
                break;
                
            case SDL_EVENT_KEY_UP :
                if (event.key.scancode == SDL_SCANCODE_F12)
                {
                    if (!rtData.config.enableReset)
                        break;
                    
                    resetting = false;
                    rtData.rqResetFinish = true;
                    break;
                }
                
                keyStates[event.key.scancode] = false;
                break;
                
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                controllerState.buttons[event.gbutton.button] = true;
                break;
                
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                controllerState.buttons[event.gbutton.button] = false;
                break;
                
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                controllerState.axes[event.gaxis.axis] = event.gaxis.value;
                break;
                
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!ctrl)
                    ctrl = SDL_OpenGamepad(event.gdevice.which);
                break;
                
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (ctrl && SDL_GetGamepadID(ctrl) == event.gdevice.which) {
                    SDL_CloseGamepad(ctrl);
                    ctrl = nullptr;
                    resetInputStates();
                }
                break;
                
            case SDL_EVENT_MOUSE_BUTTON_DOWN :
                mouseState.buttons[event.button.button] = true;
                break;
                
            case SDL_EVENT_MOUSE_BUTTON_UP :
                mouseState.buttons[event.button.button] = false;
                break;
                
            case SDL_EVENT_MOUSE_MOTION :
                mouseState.x = event.motion.x;
                mouseState.y = event.motion.y;
                cursorTimer();
                updateCursorState(cursorInWindow, gameScreen);
                break;
                
            case SDL_EVENT_MOUSE_WHEEL :
                /* Only consider vertical scrolling for now */
                verticalScrollDistance += static_cast<int>(event.wheel.y);
                
            case SDL_EVENT_FINGER_DOWN :
                i = event.tfinger.fingerID;
                touchState.fingers[i].down = true;
                
            case SDL_EVENT_FINGER_MOTION :
                i = event.tfinger.fingerID;
                touchState.fingers[i].x = event.tfinger.x * winW;
                touchState.fingers[i].y = event.tfinger.y * winH;
                break;
                
            case SDL_EVENT_FINGER_UP :
                i = event.tfinger.fingerID;
                memset(&touchState.fingers[i], 0, sizeof(touchState.fingers[0]));
                break;
                
            default :
                /* Handle user events */
                switch(event.type - usrIdStart)
                {
                    case REQUEST_SETFULLSCREEN :
                        setFullscreen(win, static_cast<bool>(event.user.code));
                        break;
                        
                    case REQUEST_WINRESIZE :
                        SDL_SetWindowSize(win, event.window.data1, event.window.data2);
                        rtData.rqWindowAdjust = false;
                        rtData.rqWindowAdjust.notify_all();
                        break;
                        
                    case REQUEST_WINCENTER :
                        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                        rtData.rqWindowAdjust = false;
                        rtData.rqWindowAdjust.notify_all();
                        break;
                        
                    case REQUEST_WINRENAME :
                        rtData.config.windowTitle = (const char*)event.user.data1;
                        SDL_SetWindowTitle(win, rtData.config.windowTitle.c_str());
                        break;
                        
                    case REQUEST_TEXTMODE :
                        if (event.user.code)
                        {
                            {
                                const SDL_Rect rect = {0, 0, 1, 1};
                                SDL_SetTextInputArea(win, &rect, 0);
                            }
                            SDL_StartTextInput(win);
                            { auto guard = lockText(); textInputBuffer.clear(); }
                        }
                        else
                        {
                            SDL_StopTextInput(win);
                            { auto guard = lockText(); textInputBuffer.clear(); }
                        }
                        break;
                        
                    case REQUEST_MESSAGEBOX :
                    {
                        SDL_ShowSimpleMessageBox(event.user.code,
                                                 rtData.config.windowTitle.c_str(),
                                                 (const char*)event.user.data1, win);
                        SDL_free(event.user.data1);
                        msgBoxDone = true;
                        break;
                    }
                    case REQUEST_SETCURSORVISIBLE :
                        showCursor = event.user.code;
                        updateCursorState(cursorInWindow, gameScreen);
                        break;
                        
                    case REQUEST_SETTINGS :
#ifndef SDL_PLATFORM_APPLE
                        if (!sMenu)
                        {
                            sMenu = std::make_unique<SettingsMenu>(rtData);
                            updateCursorState(false, gameScreen);
                        }
                        
                        sMenu->raise();
#else
                        openSettingsWindow();
#endif
                        break;
                        
                    case UPDATE_FPS :
                        if (rtData.config.printFPS)
                            Debug() << "FPS:" << event.user.code;
                        
                        if (!fps.sendUpdates)
                            break;
                        
                        title = std::format("{} - {} FPS", rtData.config.windowTitle, event.user.code);
                        
                        /* Updating the window title in fullscreen
                         * mode seems to cause flickering */
                        if (fullscreen)
                        {
                            pendingTitle = title;
                            havePendingTitle = true;
                            
                            break;
                        }
                        
                        SDL_SetWindowTitle(win, title.c_str());
                        break;
                        
                    case UPDATE_SCREEN_RECT :
                        gameScreen.x = event.user.windowID;
                        gameScreen.y = event.user.code;
                        gameScreen.w = reinterpret_cast<intptr_t>(event.user.data1);
                        gameScreen.h = reinterpret_cast<intptr_t>(event.user.data2);
                        updateCursorState(cursorInWindow, gameScreen);
                        
                        break;
                }
        }
        
        if (terminate)
            break;
    }
    
    if (SDL_GamepadConnected(ctrl))
        SDL_CloseGamepad(ctrl);
    
#ifndef SDL_PLATFORM_APPLE
    sMenu.reset();
#endif
}

void EventThread::cleanup()
{
    SDL_Event event;
    
    while (SDL_PollEvent(&event))
        if ((event.type - usrIdStart) == REQUEST_MESSAGEBOX)
            SDL_free(event.user.data1);
}

void EventThread::resetInputStates()
{
    memset(&keyStates, 0, sizeof(keyStates));
    memset(&controllerState, 0, sizeof(controllerState));
    memset(&mouseState.buttons, 0, sizeof(mouseState.buttons));
    memset(&touchState, 0, sizeof(touchState));
}

void EventThread::setFullscreen(SDL_Window *win, bool mode)
{
    SDL_SetWindowFullscreen
    (win, mode);
    fullscreen = mode;
}

void EventThread::updateCursorState(bool inWindow,
                                    const SDL_Rect &screen)
{
    SDL_Point pos = { mouseState.x, mouseState.y };
    bool inScreen = inWindow && SDL_PointInRect(&pos, &screen);
    
    if (inScreen)
        (showCursor || hideCursorTimerID) ? SDL_ShowCursor() : SDL_HideCursor();
    else
        SDL_ShowCursor();
}

bool EventThread::requestGpuPresent()
{
    if (gpuPresentQueued.exchange(true)) return true;
    SDL_Event event{};
    event.type = usrIdStart + REQUEST_GPU_PRESENT;
    if (SDL_PushEvent(&event)) return true;
    gpuPresentQueued = false;
    return false;
}

void EventThread::requestTerminate()
{
    SDL_Event event;
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

void EventThread::requestFullscreenMode(bool mode)
{
    if (mode == fullscreen)
        return;
    
    SDL_Event event;
    event.type = usrIdStart + REQUEST_SETFULLSCREEN;
    event.user.code = static_cast<Sint32>(mode);
    SDL_PushEvent(&event);
}

void EventThread::requestWindowResize(int width, int height)
{
    shState->rtData().rqWindowAdjust = true;
    SDL_Event event;
    event.type = usrIdStart + REQUEST_WINRESIZE;
    event.window.data1 = width;
    event.window.data2 = height;
    SDL_PushEvent(&event);
}

void EventThread::requestWindowCenter()
{
    shState->rtData().rqWindowAdjust = true;
    SDL_Event event;
    event.type = usrIdStart + REQUEST_WINCENTER;
    SDL_PushEvent(&event);
}

void EventThread::requestWindowRename(const char *title)
{
    SDL_Event event;
    event.type = usrIdStart + REQUEST_WINRENAME;
    event.user.data1 = (void*)title;
    SDL_PushEvent(&event);
}

void EventThread::requestShowCursor(bool mode)
{
    SDL_Event event;
    event.type = usrIdStart + REQUEST_SETCURSORVISIBLE;
    event.user.code = mode;
    SDL_PushEvent(&event);
}

void EventThread::requestTextInputMode(bool mode)
{
    SDL_Event event;
    event.type = usrIdStart + REQUEST_TEXTMODE;
    event.user.code = mode;
    SDL_PushEvent(&event);
}

void EventThread::requestSettingsMenu()
{
    SDL_Event event;
    event.type = usrIdStart + REQUEST_SETTINGS;
    SDL_PushEvent(&event);
}

void EventThread::showMessageBox(const char *body, int flags)
{
    msgBoxDone = false;
    
    // mkxp has already been asked to quit.
    // Don't break things if the window wants to close
    if (shState->rtData().rqTerm)
        return;
    
    SDL_Event event;
    event.user.code = flags;
    event.user.data1 = SDL_strdup(body);
    event.type = usrIdStart + REQUEST_MESSAGEBOX;
    SDL_PushEvent(&event);
    
    /* Keep repainting screen while box is open */
    try{
        shState->graphics().repaintWait(msgBoxDone);
    }catch(...){}
    /* Prevent endless loops */
    resetInputStates();
}

bool EventThread::getFullscreen() const
{
    return fullscreen;
}

bool EventThread::getShowCursor() const
{
    return showCursor;
}

bool EventThread::getControllerConnected() const
{
    return ctrl != 0;
}

SDL_Gamepad *EventThread::controller() const
{
    return ctrl;
}

void EventThread::notifyFrame()
{
#ifdef SDL_PLATFORM_APPLE
    uint32_t frames = round(shState->graphics().averageFrameRate());
    updateTouchBarFPSDisplay(frames);
#endif
    if (!fps.sendUpdates)
        return;
    
    SDL_Event event;
#ifdef SDL_PLATFORM_APPLE
    event.user.code = frames;
#else
    event.user.code = round(shState->graphics().averageFrameRate());
#endif
    event.user.type = usrIdStart + UPDATE_FPS;
    SDL_PushEvent(&event);
}

void EventThread::notifyGameScreenChange(const SDL_Rect &screen)
{
    /* We have to get a bit hacky here to fit the rectangle
     * data into the user event struct */
    SDL_Event event;
    event.type = usrIdStart + UPDATE_SCREEN_RECT;
    event.user.windowID = screen.x;
    event.user.code = screen.y;
    event.user.data1 = reinterpret_cast<void*>(screen.w);
    event.user.data2 = reinterpret_cast<void*>(screen.h);
    SDL_PushEvent(&event);
}
