/*
** eventthread.h
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

#ifndef EVENTTHREAD_H
#define EVENTTHREAD_H

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_gamepad.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

#include <stdint.h>

#include "config.h"
#include "etc-internal.h"
#include "sdl-util.h"
#include "keybindings.h"

struct RGSSThreadData;
struct SDL_Window;
union SDL_Event;

#define MAX_FINGERS 4

class EventThread
{
public:
    
    struct ControllerState {
        int axes[SDL_GAMEPAD_AXIS_COUNT];
        bool buttons[SDL_GAMEPAD_BUTTON_COUNT];
    };

	struct MouseState
	{
		int x, y;
		bool inWindow;
		bool buttons[32];
	};

	struct FingerState
	{
		bool down;
		int x, y;
	};

	struct TouchState
	{
		FingerState fingers[MAX_FINGERS];
	};

	static uint8_t keyStates[SDL_SCANCODE_COUNT];
    static ControllerState controllerState;
	static MouseState mouseState;
	static TouchState touchState;
    static std::atomic<int> verticalScrollDistance;
    
    std::string textInputBuffer;
    [[nodiscard]] std::unique_lock<std::mutex> lockText() { return std::unique_lock(textInputLock); }
    

	static bool allocUserEvents();

	EventThread();

	void process(RGSSThreadData &rtData);
	void cleanup();

	/* Called from RGSS thread */
	void requestFullscreenMode(bool mode);
	void requestWindowResize(int width, int height);
    void requestWindowCenter();
    void requestWindowRename(const char *title);
	void requestShowCursor(bool mode);
    
    void requestTextInputMode(bool mode);
    
    void requestSettingsMenu();

	void requestTerminate();

	bool getFullscreen() const;
	bool getShowCursor() const;
    bool getControllerConnected() const;
    
    SDL_Gamepad *controller() const;

	void showMessageBox(const char *body, int flags = 0);

	/* RGSS thread calls this once per frame */
	void notifyFrame();

	/* Called on game screen (size / offset) changes */
	void notifyGameScreenChange(const SDL_Rect &screen);

private:
	static bool eventFilter(void *, SDL_Event*);

	void resetInputStates();
	void setFullscreen(SDL_Window *, bool mode);
	void updateCursorState(bool inWindow,
	                       const SDL_Rect &screen);
	void cursorTimer();

	bool fullscreen;
	bool showCursor;
    
    SDL_Gamepad *ctrl;
    
	std::atomic<bool> msgBoxDone{false};
    
    std::mutex textInputLock;

	struct
	{
		std::atomic<bool> sendUpdates{false};
	} fps;
};

/* Used to asynchronously inform the RGSS thread
 * about certain value changes */
template<typename T>
struct UnidirMessage
{
	UnidirMessage()
	    : current(T())
	{}

	/* Done from the sending side */
	void post(const T &value)
	{
		std::scoped_lock guard(mutex);

		changed = true;
		current = value;
	}

	/* Done from the receiving side */
	bool poll(T &out) const
	{
		if (!changed)
			return false;

		std::scoped_lock guard(mutex);

		out = current;
		changed = false;

		return true;
	}

	/* Done from either */
	void get(T &out) const
	{
		std::scoped_lock guard(mutex);
		out = current;
	}

private:
	mutable std::mutex mutex;
	mutable std::atomic<bool> changed{false};
	T current;
};

struct SyncPoint
{
	/* Used by eventFilter to control sleep/wakeup */
	void haltThreads();
	void resumeThreads();

	/* Used by RGSS thread */
	bool mainSyncLocked();
	void waitMainSync();

	/* Used by secondary (audio) threads */
	void passSecondarySync();

private:
	struct Util
	{
		void lock();
		void unlock(bool multi);
		void waitForUnlock();

		std::atomic<bool> locked{false};
		std::mutex mut;
		std::condition_variable cond;
	};

	Util mainSync;
	Util reply;
	Util secondSync;
};

struct RGSSThreadData
{
	/* Main thread sets this to request RGSS thread to terminate */
	std::atomic<bool> rqTerm{false};
	/* In response, RGSS thread sets this to confirm
	 * that it received the request and isn't stuck */
	std::atomic<bool> rqTermAck{false};

	/* Set when F12 is pressed */
	std::atomic<bool> rqReset{false};

	/* Set when F12 is released */
	std::atomic<bool> rqResetFinish{false};
    
    // Set when window is being adjusted (resize, reposition)
    std::atomic<bool> rqWindowAdjust{false};

	EventThread *ethread;
	UnidirMessage<Vec2i> windowSizeMsg;
    UnidirMessage<Vec2i> drawableSizeMsg;
	UnidirMessage<BDescVec> bindingUpdateMsg;
	SyncPoint syncPoint;

	const char *argv0;

	SDL_Window *window;
    
    SDL_GLContext glContext;

	Vec2 sizeResoRatio;
	Vec2i screenOffset;
    int scale;
	const int refreshRate;

	Config config;

	std::string rgssErrorMsg;

	RGSSThreadData(EventThread *ethread,
	               const char *argv0,
	               SDL_Window *window,
	               int refreshRate,
                   int scalingFactor,
	               const Config& newconf,
                   SDL_GLContext ctx)
	    : ethread(ethread),
	      argv0(argv0),
	      window(window),
	      sizeResoRatio(1, 1),
	      refreshRate(refreshRate),
          scale(scalingFactor),
	      config(newconf),
          glContext(ctx)
	{
		rqResetFinish = true;
	}
};

#endif // EVENTTHREAD_H
