/*
 ** graphics.cpp
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

#include "graphics.h"

#include <memory>

#include "audio.h"
#include "binding.h"
#include "bitmap.h"
#include "config.h"
#include "debugwriter.h"
#include "disposable.h"
#include "etc.h"
#include "etc-internal.h"
#include "eventthread.h"
#include "filesystem.h"
#include "display/gpu-render-context.h"
#include "display/gpu-frame-handoff.h"
#include "render-resources.h"
#include "render-ops.h"
#include "render-state.h"
#include "intrulist.h"
#include "quad.h"
#include "scene.h"
#include "shader.h"
#include "sharedstate.h"
#include "texpool.h"
#include "util.h"
#include "input.h"
#include "sprite.h"

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <mutex>

#include <algorithm>
#include <functional>
#include <numeric>
#include <cmath>
#include <climits>


#define DEF_SCREEN_W 640
#define DEF_SCREEN_H 480

#define DEF_FRAMERATE 40

struct PingPong {
    TEXFBO rt[2];
    uint8_t srcInd, dstInd;
    int screenW, screenH;
    
    PingPong(int screenW, int screenH)
    : srcInd(0), dstInd(1), screenW(screenW), screenH(screenH) {
        for (int i = 0; i < 2; ++i) {
            TEXFBO::init(rt[i]);
            TEXFBO::allocEmpty(rt[i], screenW, screenH);
            TEXFBO::linkFBO(rt[i]);
            gpu::RenderContext::current().setClearColor({0, 0, 0, 1});
            FBO::clear();
        }
    }
    
    ~PingPong() {
        for (int i = 0; i < 2; ++i)
            TEXFBO::fini(rt[i]);
    }
    
    TEXFBO &backBuffer() { return rt[srcInd]; }
    
    TEXFBO &frontBuffer() { return rt[dstInd]; }
    
    /* Better not call this during render cycles */
    void resize(int width, int height) {
        screenW = width;
        screenH = height;
        
        for (int i = 0; i < 2; ++i)
            TEXFBO::allocEmpty(rt[i], width, height);
    }
    
    void startRender() { bind(); }
    
    void swapRender() {
        std::swap(srcInd, dstInd);
        
        bind();
    }
    
    void clearBuffers() {
        renderState.clearColor.pushSet(Vec4(0, 0, 0, 1));
        
        for (int i = 0; i < 2; ++i) {
            FBO::bind(rt[i].fbo);
            FBO::clear();
        }
        
        renderState.clearColor.pop();
    }
    
private:
    void bind() { FBO::bind(rt[dstInd].fbo); }
};

class ScreenScene : public Scene {
public:
    ScreenScene(int width, int height) : pp(width, height) {
        updateReso(width, height);
        
        brightEffect = false;
        brightnessQuad.setColor(Vec4());
    }
    
    void composite() {
        const int w = geometry.rect.w;
        const int h = geometry.rect.h;
        
        shState->prepareDraw();
        
        pp.startRender();
        
        renderState.viewport.set(IntRect(0, 0, w, h));
        
        FBO::clear();
        
        Scene::composite();
        
        if (brightEffect) {
            SimpleColorShader &shader = shState->shaders().simpleColor;
            shader.bind();
            shader.applyViewportProj();
            shader.setTranslation(Vec2i());
            
            brightnessQuad.draw();
        }
    }
    
    void requestViewportRender(const Vec4 &c, const Vec4 &f, const Vec4 &t) {
        const IntRect &viewpRect = renderState.scissorBox.get();
        const IntRect &screenRect = geometry.rect;
        
        const bool toneRGBEffect = t.xyzNotNull();
        const bool toneGrayEffect = t.w != 0;
        const bool colorEffect = c.w > 0;
        const bool flashEffect = f.w > 0;
        
        if (toneGrayEffect) {
            pp.swapRender();
            
            if (!viewpRect.encloses(screenRect)) {
                /* Scissor test _does_ affect FBO blit operations,
                 * and since we're inside the draw cycle, it will
                 * be turned on, so turn it off temporarily */
                renderState.scissorTest.pushSet(false);
                
                int scaleIsSpecial = RenderOps::blitScaleIsSpecial(pp.frontBuffer(), false, geometry.rect, pp.backBuffer(), geometry.rect);

                RenderOps::blitBegin(pp.frontBuffer(), false, scaleIsSpecial);
                RenderOps::blitSource(pp.backBuffer(), scaleIsSpecial);
                RenderOps::blitRectangle(geometry.rect, Vec2i());
                RenderOps::blitEnd();
                
                renderState.scissorTest.pop();
            }
            
            GrayShader &shader = shState->shaders().gray;
            shader.bind();
            shader.setGray(t.w);
            shader.applyViewportProj();
            shader.setTexSize(screenRect.size());
            
            TEX::bind(pp.backBuffer().tex);
            
            renderState.blend.pushSet(false);
            screenQuad.draw();
            renderState.blend.pop();
        }
        
        if (!toneRGBEffect && !colorEffect && !flashEffect)
            return;
        
        FlatColorShader &shader = shState->shaders().flatColor;
        shader.bind();
        shader.applyViewportProj();
        
        if (toneRGBEffect) {
            /* First split up additive / substractive components */
            Vec4 add, sub;
            
            if (t.x > 0)
                add.x = t.x;
            if (t.y > 0)
                add.y = t.y;
            if (t.z > 0)
                add.z = t.z;
            
            if (t.x < 0)
                sub.x = -t.x;
            if (t.y < 0)
                sub.y = -t.y;
            if (t.z < 0)
                sub.z = -t.z;
            
            /* Then apply them using hardware blending */

            
            if (add.xyzNotNull()) {
                gpu::RenderContext::current().setBlend(gpu::Blend::ColorAdditionKeepDestinationAlpha);
                shader.setColor(add);
                
                screenQuad.draw();
            }
            
            if (sub.xyzNotNull()) {
                gpu::RenderContext::current().setBlend(gpu::Blend::ColorSubtractionKeepDestinationAlpha);
                shader.setColor(sub);
                
                screenQuad.draw();
            }
        }
        
        if (colorEffect || flashEffect) {
            gpu::RenderContext::current().setBlend(gpu::Blend::KeepDestinationAlpha);
        }
        
        if (colorEffect) {
            shader.setColor(c);
            screenQuad.draw();
        }
        
        if (flashEffect) {
            shader.setColor(f);
            screenQuad.draw();
        }
        
        renderState.blendMode.refresh();
    }
    
    void setBrightness(float norm) {
        brightnessQuad.setColor(Vec4(0, 0, 0, 1.0f - norm));
        
        brightEffect = norm < 1.0f;
    }
    
    void updateReso(int width, int height) {
        geometry.rect.w = width;
        geometry.rect.h = height;
        
        screenQuad.setTexPosRect(geometry.rect, geometry.rect);
        brightnessQuad.setTexPosRect(geometry.rect, geometry.rect);
        
        notifyGeometryChange();
    }
    
    void setResolution(int width, int height) {
        pp.resize(width, height);
        updateReso(width, height);
    }
    
    PingPong &getPP() { return pp; }
    
private:
    PingPong pp;
    Quad screenQuad;
    
    Quad brightnessQuad;
    bool brightEffect;
};

/* All timestamps and durations are SDL_GetTicksNS() nanoseconds. */
struct FPSLimiter {
    Uint64 lastTickCount;
    
    /* nanoseconds per frame */
    Sint64 tpf;
    
    bool disabled;
    
    /* Data for frame timing adjustment */
    struct {
        /* Last tick count */
        Uint64 last;
        
        /* How far behind/in front we are for ideal frame timing */
        Sint64 idealDiff;
        
        bool resetFlag;
    } adj;
    
    FPSLimiter(uint16_t desiredFPS)
    : lastTickCount(SDL_GetTicksNS()), disabled(false) {
        setDesiredFPS(desiredFPS);
        
        adj.last = lastTickCount;
        adj.idealDiff = 0;
        adj.resetFlag = false;
    }
    
    void setDesiredFPS(uint16_t value) { tpf = SDL_NS_PER_SECOND / value; }
    
    void delay() {
        if (disabled)
            return;
        
        Sint64 tickDelta = SDL_GetTicksNS() - lastTickCount;
        Sint64 toDelay = tpf - tickDelta;
        
        /* Compensate for the last delta
         * to the ideal timestep */
        toDelay -= adj.idealDiff;
        
        if (toDelay > 0)
            SDL_DelayPrecise(toDelay);
        
        Uint64 now = lastTickCount = SDL_GetTicksNS();
        Sint64 diff = now - adj.last;
        adj.last = now;
        
        /* Recalculate our temporal position
         * relative to the ideal timestep */
        adj.idealDiff = diff - tpf + adj.idealDiff;
        
        if (adj.resetFlag) {
            adj.idealDiff = 0;
            adj.resetFlag = false;
        }
    }
    
    void resetFrameAdjust() { adj.resetFlag = true; }
    
    /* If we're more than a full frame's worth
     * of ticks behind the ideal timestep,
     * there's no choice but to skip frame(s)
     * to catch up */
    bool frameSkipRequired() const {
        if (disabled)
            return false;
        
        return adj.idealDiff > tpf;
    }
};

struct GraphicsPrivate {
    /* Screen resolution, ie. the resolution at which
     * RGSS renders at (settable with Graphics.resize_screen).
     * Can only be changed from within RGSS */
    Vec2i scRes;
    Vec2i scResLores;
    
    /* Screen size, to which the rendered frames are scaled up.
     * This can be smaller than the window size when fixed aspect
     * ratio is enforced */
    Vec2i scSize;
    
    /* Actual physical size of the game window */
    Vec2i winSize;
    
    /* Offset in the game window at which the scaled game screen
     * is blitted inside the game window */
    Vec2i scOffset;
    
    ScreenScene screen;
    RGSSThreadData *threadData;
    TEXFBO windowCanvas;
    Uint64 presentationSequence = 0;
    
    int frameRate;
    int frameCount;
    int brightness;
    
    double last_update;
    
    
    FPSLimiter fpsLimiter;
    
    // Can be set from Ruby. Takes priority over config setting.
    bool useFrameSkip;
    
    bool frozen;
    TEXFBO frozenScene;
    Quad screenQuad;
    
    float backingScaleFactor;
    
    Vec2i integerScaleFactor;
    TEXFBO integerScaleBuffer;
    bool integerScaleActive;
    bool integerLastMileScaling;
    
    std::vector<double> avgFPSData;
    double last_avg_update;
    std::mutex avgFPSLock;
    
    // Disposal can reenter the graphics lock while a rendering operation owns it.
    std::recursive_mutex renderResourceLock;
    bool multithreadedMode;
    
    /* Global list of all live Disposables
     * (disposed on reset) */
    IntruList<Disposable> dispList;
    
    GraphicsPrivate(RGSSThreadData *rtData)
    : scResLores(DEF_SCREEN_W, DEF_SCREEN_H),
    scRes(rtData->config.enableHires ? (int)lround(rtData->config.framebufferScalingFactor * DEF_SCREEN_W) : DEF_SCREEN_W,
        rtData->config.enableHires ? (int)lround(rtData->config.framebufferScalingFactor * DEF_SCREEN_H) : DEF_SCREEN_H),
    scSize(scRes),
    winSize(rtData->config.defScreenW, rtData->config.defScreenH),
    screen(scRes.x, scRes.y), threadData(rtData),
    multithreadedMode(true),
    frameRate(DEF_FRAMERATE), frameCount(0), brightness(255),
    fpsLimiter(frameRate), useFrameSkip(rtData->config.frameSkip), frozen(false),
    last_update(0), last_avg_update(0), backingScaleFactor(1), integerScaleFactor(0, 0),
    integerScaleActive(rtData->config.integerScaling.active),
    integerLastMileScaling(rtData->config.integerScaling.lastMileScaling) {
        avgFPSData = std::vector<double>();
        WindowGeometry geometry;
        if (threadData->windowGeometryMsg.poll(geometry) && geometry.logical.x > 0 && geometry.logical.y > 0) {
            const auto &windowUnits = geometry.logical;
            const auto &drawableSize = geometry.pixels;
            if (drawableSize.x > 0 && drawableSize.y > 0) {
                backingScaleFactor = float(drawableSize.x) / windowUnits.x;
                winSize = drawableSize;
            }
        }
        ensureWindowCanvas();

        if (integerScaleActive) {
            findHighestIntegerScale();
            rebuildIntegerScaleBuffer();
        }
        
        recalculateScreenSize(rtData->config.fixedAspectRatio);
        updateScreenResoRatio(rtData);
        
        TEXFBO::init(frozenScene);
        TEXFBO::allocEmpty(frozenScene, scRes.x, scRes.y);
        TEXFBO::linkFBO(frozenScene);
        
        FloatRect screenRect(0, 0, scRes.x, scRes.y);
        screenQuad.setTexPosRect(screenRect, screenRect);
        
        fpsLimiter.resetFrameAdjust();
    }
    
    ~GraphicsPrivate() {
        TEXFBO::fini(windowCanvas);
        TEXFBO::fini(frozenScene);
        TEXFBO::fini(integerScaleBuffer);
    }
    
    void updateScreenResoRatio(RGSSThreadData *rtData) {
        Vec2 &ratio = rtData->sizeResoRatio;
        ratio.x = (float)scRes.x / scSize.x * backingScaleFactor;
        ratio.y = (float)scRes.y / scSize.y * backingScaleFactor;
        
        rtData->screenOffset = scOffset / backingScaleFactor;
    }
    
    /* Enforces fixed aspect ratio, if desired */
    void recalculateScreenSize(bool fixedAspectRatio) {
        scSize = winSize;
        
        if (!fixedAspectRatio) {
            if (!integerScaleActive || (integerScaleActive && integerLastMileScaling)) {
                scOffset = Vec2i(0, 0);
                return;
            }
        }
        
        if (integerScaleActive && !integerLastMileScaling) {
            scOffset.x = ((winSize.x / 2) - (scRes.x / 2) * integerScaleFactor.x);
            scOffset.y = ((winSize.y / 2) - (scRes.y / 2) * integerScaleFactor.y);
            
            scSize = Vec2i(scRes.x * integerScaleFactor.x, scRes.y * integerScaleFactor.y);
            return;
        }
        
        float resRatio = (float)scRes.x / scRes.y;
        float winRatio = (float)winSize.x / winSize.y;
        
        if (resRatio > winRatio)
            scSize.y = scSize.x / resRatio;
        else if (resRatio < winRatio)
            scSize.x = scSize.y * resRatio;
        
        scOffset.x = (winSize.x - scSize.x) / 2.f;
        scOffset.y = (winSize.y - scSize.y) / 2.f;
    }
    
    static int findHighestFittingScale(int base, int target) {
        int scale = 1;
        
        while (base * scale <= target)
            scale++;
        
        return std::max(scale - 1, 1);
    }
    
    /* Returns whether a new scale was found */
    bool findHighestIntegerScale()
    {
        Vec2i newScale(findHighestFittingScale(scRes.x, winSize.x),
                       findHighestFittingScale(scRes.y, winSize.y));
        
        if (threadData->config.fixedAspectRatio)
        {
            /* Limit both factors to the smaller of the two */
            newScale.x = newScale.y = std::min(newScale.x, newScale.y);
        }
        
        if (newScale == integerScaleFactor)
            return false;
        
        integerScaleFactor = newScale;
        return true;
    }
    
    void rebuildIntegerScaleBuffer()
    {
        TEXFBO::fini(integerScaleBuffer);
        TEXFBO::init(integerScaleBuffer);
        TEXFBO::allocEmpty(integerScaleBuffer, scRes.x * integerScaleFactor.x,
                           scRes.y * integerScaleFactor.y);
        TEXFBO::linkFBO(integerScaleBuffer);
    }
    
    bool integerScaleStepApplicable() const
    {
        if (!integerScaleActive)
            return false;
        
        if (integerScaleFactor.x < 1 || integerScaleFactor.y < 1) // XXX should be < 2, this is for testing only
            return false;
        
        return true;
    }
    
    void checkResize(bool skipIntScaleBuffer = false) {
        WindowGeometry geometry;
        if (threadData->windowGeometryMsg.poll(geometry)) {
            const auto &windowUnits = geometry.logical;
            const auto &drawableSize = geometry.pixels;
            if (windowUnits.x <= 0 || windowUnits.y <= 0) return;
            if (drawableSize.x <= 0 || drawableSize.y <= 0) return;
            backingScaleFactor = float(drawableSize.x) / windowUnits.x;
            winSize = drawableSize;
            
            /* Make sure integer buffers are rebuilt before screen offsets are
             * calculated so we have the final allocated buffer size ready */
            if (integerScaleActive && findHighestIntegerScale() && !skipIntScaleBuffer)
                rebuildIntegerScaleBuffer();
            
            /* Restore the offscreen render viewport after a window-size change. */
            renderState.viewport.refresh();
            recalculateScreenSize(threadData->config.fixedAspectRatio);
            updateScreenResoRatio(threadData);
            
            SDL_Rect screen = {scOffset.x, scOffset.y, scSize.x, scSize.y};
            threadData->ethread->notifyGameScreenChange(screen);
        }
    }
    
    void checkShutDownReset() {
        shState->checkShutdown();
        shState->checkReset();
    }
    
    void shutdown() {
        threadData->rqTermAck = true;
        shState->texPool().disable();
        
        scriptBinding->terminate();
    }
    
private:
    bool ensureWindowCanvas() {
        if (winSize.x <= 0 || winSize.y <= 0) return false;
        if (windowCanvas.tex.empty()) TEXFBO::init(windowCanvas);
        if (windowCanvas.width != winSize.x || windowCanvas.height != winSize.y) {
            gpu::RenderContext::current().flush();
            TEXFBO::allocEmpty(windowCanvas, winSize.x, winSize.y);
            TEXFBO::linkFBO(windowCanvas);
            gpu::RenderContext::current().setDefaultTarget(gpu::TextureView(windowCanvas.tex.resource()));
        }
        return true;
    }

    void publishWindowCanvas() {
        try {
            auto &context = gpu::RenderContext::current();
            context.flush();
            auto &device = context.device();
            // Keep the producer's canvas private: presentation owns this snapshot
            // until its ready fence and window blit have completed.
            auto snapshot = device.createTexture(windowCanvas.width, windowCanvas.height);
            gpu::CommandBuffer command(device.native());
            device.copy(command, windowCanvas.tex.resource(), snapshot);
            auto ready = command.submitWithFence();
            if (!threadData->gpuPresentation->publish({std::move(snapshot), std::move(ready), ++presentationSequence})) {
                if (threadData->rqTerm) return;
                throw Exception(Exception::SDLError, "GPU presentation queue is closed");
            }
            if (!threadData->ethread->requestGpuPresent()) {
                threadData->gpuPresentation->close();
                throw Exception(Exception::SDLError, "Could not queue GPU presentation event");
            }
        } catch (const std::exception &error) {
            throw Exception(Exception::SDLError, "%s", error.what());
        }
    }

    template <typename... Args> void swapScreenBufferImpl(const Vec2i &, TEXFBO &source, int scaleIsSpecial, Args... args) {
        fpsLimiter.delay();
        if (ensureWindowCanvas()) {
            RenderOps::blitBeginScreen(winSize, scaleIsSpecial);
            RenderOps::blitSource(source, scaleIsSpecial);
            renderState.clearColor.pushSet(Vec4(0, 0, 0, 1));
            FBO::clear();
            renderState.clearColor.pop();
            blitScreenScaled(args...);
            RenderOps::blitEnd();
            publishWindowCanvas();
        }
        ++frameCount;
        threadData->ethread->notifyFrame();
    }

public:
    void swapScreenBuffer(const Vec2i &screenSize, TEXFBO &source, int scaleIsSpecial) {
        swapScreenBufferImpl(screenSize, source, scaleIsSpecial, scaleIsSpecial);
    }

    void swapScreenBuffer(const Vec2i &screenSize, TEXFBO &source, const Vec2i &sourceSize, int scaleIsSpecial) {
        swapScreenBufferImpl(screenSize, source, scaleIsSpecial, sourceSize, scaleIsSpecial);
    }

    void swapScreenBuffer(const Vec2i &screenSize, TEXFBO &source, const Vec2i &sourceSize, int scaleIsSpecial, bool forceNearestNeighbor) {
        swapScreenBufferImpl(screenSize, source, scaleIsSpecial, sourceSize, scaleIsSpecial, forceNearestNeighbor);
    }
    
    void compositeToBuffer(TEXFBO &buffer) {
        compositeToBufferScaled(buffer, scRes.x, scRes.y);
    }

    void compositeToBufferScaled(TEXFBO &buffer, int destWidth, int destHeight) {
        screen.composite();
        
        int scaleIsSpecial = RenderOps::blitScaleIsSpecial(buffer, false, IntRect(0, 0, destWidth, destHeight), screen.getPP().frontBuffer(), IntRect(0, 0, scRes.x, scRes.y));

        RenderOps::blitBegin(buffer, false, scaleIsSpecial);
        RenderOps::blitSource(screen.getPP().frontBuffer(), scaleIsSpecial);
        RenderOps::blitRectangle(IntRect(0, 0, scRes.x, scRes.y), IntRect(0, 0, destWidth, destHeight));
        RenderOps::blitEnd();
    }
    
    void blitScreenScaled(int scaleIsSpecial) {
        blitScreenScaled(scRes, scaleIsSpecial);
    }

    void blitScreenScaled(const Vec2i &sourceSize, int scaleIsSpecial, bool forceNearestNeighbor=false) {
        RenderOps::blitRectangle(IntRect(0, 0, sourceSize.x, sourceSize.y),
                              IntRect(scOffset.x, scOffset.y, scSize.x, scSize.y),
                              !forceNearestNeighbor && RenderOps::smoothScalingMethod(scaleIsSpecial) == Bilinear);
    }

    void redrawScreen() {
        screen.composite();
        
        // maybe unspaghetti this later
        if (integerScaleStepApplicable() && !integerLastMileScaling)
        {
            int scaleIsSpecial = RenderOps::blitScaleIsSpecial(integerScaleBuffer, false, IntRect(0, 0, scSize.x, scSize.y), screen.getPP().frontBuffer(), IntRect(0, 0, scRes.x, scRes.y));

            swapScreenBuffer(winSize, screen.getPP().frontBuffer(), scRes, scaleIsSpecial, true);
            updateAvgFPS();
            return;
        }
        
        if (integerScaleStepApplicable())
        {
            int scaleIsSpecial = RenderOps::blitScaleIsSpecial(integerScaleBuffer, false, IntRect(0, 0, integerScaleBuffer.width, integerScaleBuffer.height), screen.getPP().frontBuffer(), IntRect(0, 0, scRes.x, scRes.y));

            assert(integerScaleBuffer.tex != TEX::ID(0));
            RenderOps::blitBegin(integerScaleBuffer, false, scaleIsSpecial);
            RenderOps::blitSource(screen.getPP().frontBuffer(), scaleIsSpecial);
            
            RenderOps::blitRectangle(IntRect(0, 0, scRes.x, scRes.y),
                                  IntRect(0, 0, integerScaleBuffer.width, integerScaleBuffer.height),
                                  false);
            
            RenderOps::blitEnd();
        }
        

        Vec2i sourceSize;

        if (integerScaleActive)
        {
            sourceSize = Vec2i(integerScaleBuffer.width, integerScaleBuffer.height);
        }
        else
        {
            sourceSize = scRes;
        }

        int scaleIsSpecial = RenderOps::blitScaleIsSpecial(integerScaleBuffer, false, IntRect(0, 0, scSize.x, scSize.y), integerScaleActive ? integerScaleBuffer : screen.getPP().frontBuffer(), IntRect(0, 0, sourceSize.x, sourceSize.y));

        swapScreenBuffer(winSize, integerScaleActive ? integerScaleBuffer : screen.getPP().frontBuffer(), sourceSize, scaleIsSpecial);
        
        updateAvgFPS();
    }
    
    double averageFPS() {
        std::scoped_lock lock(avgFPSLock);
        double ret = std::ranges::fold_left(avgFPSData, 0.0, std::plus{});
        return 1 / (ret / avgFPSData.size());
    }
    
    void setLock(bool force = false) {
        if (!(force || multithreadedMode)) return;
        
        renderResourceLock.lock();
    }
    
    void releaseLock(bool force = false) {
        if (!(force || multithreadedMode)) return;
        
        renderResourceLock.unlock();
    }

    void updateAvgFPS() {
        std::scoped_lock lock(avgFPSLock);
        if (avgFPSData.size() > 40)
            avgFPSData.erase(avgFPSData.begin());
        
        double time = shState->runTime();
        avgFPSData.push_back(time - last_avg_update);
        last_avg_update = time;
    }
};

Graphics::Graphics(RGSSThreadData *data) {
    p = new GraphicsPrivate(data);
    if (data->config.syncToRefreshrate) {
        p->frameRate = data->refreshRate;
        p->fpsLimiter.disabled = true;
    } else if (data->config.fixedFramerate > 0) {
        p->fpsLimiter.setDesiredFPS(data->config.fixedFramerate);
    } else if (data->config.fixedFramerate < 0) {
        p->fpsLimiter.disabled = true;
    }
}

Graphics::~Graphics() { delete p; }

double Graphics::getDelta() {
    return shState->runTime() - p->last_update;
}

double Graphics::lastUpdate() {
    return p->last_update;
}

void Graphics::update(bool checkForShutdown) {
    p->threadData->rqWindowAdjust.wait(true);
    p->last_update = shState->runTime();
    
    // update Input.repeat timing, rounding the framerate to the nearest 2
    {
        static const double mult = 2.0;
        double afr = std::abs(averageFrameRate()); // abs shouldn't be necessary but that's ok
        afr += mult / 2;
        afr -= std::fmod(afr, mult);
        shState->input().recalcRepeat(std::floor(afr));
    }
    
    if (checkForShutdown)
        p->checkShutDownReset();
    
    
    
    if (p->frozen)
        return;
    
    if (p->fpsLimiter.frameSkipRequired()) {
        if (p->useFrameSkip) {
            /* Skip frame */
            p->fpsLimiter.delay();
            ++p->frameCount;
            p->threadData->ethread->notifyFrame();
            
            return;
        } else {
            /* Just reset frame adjust counter */
            p->fpsLimiter.resetFrameAdjust();
        }
    }
    
    p->checkResize();
    p->redrawScreen();
}

void Graphics::freeze() {
    p->frozen = true;
    
    p->checkShutDownReset();
    p->checkResize();
    
    /* Capture scene into frozen buffer */
    p->compositeToBuffer(p->frozenScene);
}

void Graphics::transition(int duration, const char *filename, int vague) {
    
    if (!p->frozen)
        return;
    
    vague = clamp(vague, 1, 256);
    Bitmap *transMap = *filename ? new Bitmap(filename) : 0;
    
    setBrightness(255);
    
    /* Capture new scene */
    p->screen.composite();
    
    /* The PP frontbuffer will hold the current scene after the
     * composition step. Since the backbuffer is unused during
     * the transition, we can reuse it as the target buffer for
     * the final rendered image. */
    TEXFBO &currentScene = p->screen.getPP().frontBuffer();
    TEXFBO &transBuffer = p->screen.getPP().backBuffer();
    
    /* If no transition bitmap is provided,
     * we can use a simplified shader */
    TransShader &transShader = shState->shaders().trans;
    SimpleTransShader &simpleShader = shState->shaders().simpleTrans;
    
    // Handle high-res.
    Vec2i transSize(p->scResLores.x, p->scResLores.y);

    if (transMap) {
        TransShader &shader = transShader;
        shader.bind();
        shader.applyViewportProj();
        shader.setFrozenScene(p->frozenScene.tex);
        shader.setCurrentScene(currentScene.tex);
        /* Thresholds are sampled by normalized screen coordinates, so a
         * high-resolution mask (Hires/Graphics/Transitions/...) drops in
         * unchanged and gives per-physical-row thresholds. */
        Bitmap *maskSource = transMap->hasHires() ? transMap->getHires() : transMap;
        shader.setTransMap(maskSource->getRenderTarget().tex);
        shader.setVague(vague / 256.0f);
        shader.setTexSize(transSize);
    } else {
        SimpleTransShader &shader = simpleShader;
        shader.bind();
        shader.applyViewportProj();
        shader.setFrozenScene(p->frozenScene.tex);
        shader.setCurrentScene(currentScene.tex);
        shader.setTexSize(transSize);
    }
    
    renderState.blend.pushSet(false);
    
    for (int i = 0; i < duration; ++i) {
        /* We need to clean up transMap properly before
         * a possible longjmp, so we manually test for
         * shutdown/reset here */
        if (p->threadData->rqTerm) {
            renderState.blend.pop();
            delete transMap;
            p->shutdown();
            return;
        }
        
        if (p->threadData->rqReset) {
            renderState.blend.pop();
            delete transMap;
            scriptBinding->reset();
            return;
        }
        
        const float prog = i * (1.0f / duration);
        
        if (transMap) {
            transShader.bind();
            transShader.setProg(prog);
        } else {
            simpleShader.bind();
            simpleShader.setProg(prog);
        }
        
        /* Draw the composed frame to a buffer first
         * (we need this because we're skipping PingPong) */
        FBO::bind(transBuffer.fbo);
        FBO::clear();
        p->screenQuad.draw();
        
        p->checkResize();
        
        /* Then blit it flipped and scaled to the screen */
        FBO::unbind();
        FBO::clear();
        
        int scaleIsSpecial = RenderOps::blitScaleIsSpecial(p->integerScaleBuffer, false, IntRect(0, 0, p->scSize.x, p->scSize.y), transBuffer, IntRect(0, 0, p->scRes.x, p->scRes.y));

        p->swapScreenBuffer(p->winSize, transBuffer, scaleIsSpecial);
        /* Call this manually, as redrawScreen() is not called during this loop. */
        p->updateAvgFPS();
    }
    
    renderState.blend.pop();
    
    delete transMap;
    
    p->frozen = false;
}

void Graphics::frameReset() {p->fpsLimiter.resetFrameAdjust();}

static void guardDisposed() {}

DEF_ATTR_RD_SIMPLE(Graphics, FrameRate, int, p->frameRate)

DEF_ATTR_SIMPLE(Graphics, FrameCount, int, p->frameCount)

void Graphics::setFrameRate(int value) {
    p->frameRate = std::max(value, 1);
    
    if (p->threadData->config.syncToRefreshrate)
        return;
    
    if (p->threadData->config.fixedFramerate > 0)
        return;
    
    p->fpsLimiter.setDesiredFPS(p->frameRate);
    //shState->input().recalcRepeat((unsigned int)p->frameRate);
}

double Graphics::averageFrameRate() {
    return p->averageFPS();
}

void Graphics::wait(int duration) {
    for (int i = 0; i < duration; ++i) {
        p->checkShutDownReset();
        p->redrawScreen();
    }
}

void Graphics::fadeout(int duration) {
    FBO::unbind();
    
    float curr = p->brightness;
    float diff = 255.0f - curr;
    
    for (int i = duration - 1; i > -1; --i) {
        setBrightness(diff + (curr / duration) * i);
        
        if (p->frozen) {
            int scaleIsSpecial = RenderOps::blitScaleIsSpecial(p->integerScaleBuffer, false, IntRect(0, 0, p->scSize.x, p->scSize.y), p->frozenScene, IntRect(0, 0, p->scRes.x, p->scRes.y));

            p->swapScreenBuffer(p->scSize, p->frozenScene, scaleIsSpecial);
        } else {
            update();
        }
    }
}

void Graphics::fadein(int duration) {
    FBO::unbind();
    
    float curr = p->brightness;
    float diff = 255.0f - curr;
    
    for (int i = 1; i <= duration; ++i) {
        setBrightness(curr + (diff / duration) * i);
        
        if (p->frozen) {
            int scaleIsSpecial = RenderOps::blitScaleIsSpecial(p->integerScaleBuffer, false, IntRect(0, 0, p->scSize.x, p->scSize.y), p->frozenScene, IntRect(0, 0, p->scRes.x, p->scRes.y));

            p->swapScreenBuffer(p->scSize, p->frozenScene, scaleIsSpecial);
        } else {
            update();
        }
    }
}

Bitmap *Graphics::snapToBitmap() {
    p->screen.composite();

    if (shState->config().enableHires) {
        // TODO: Maybe don't reconstruct this struct every time?
        TEXFBO tf;
        tf.width = width();
        tf.height = height();
        tf.selfHires = &p->screen.getPP().frontBuffer();

        return new Bitmap(tf);
    }

    return new Bitmap(p->screen.getPP().frontBuffer());
}

int Graphics::width() const { return p->scResLores.x; }

int Graphics::height() const { return p->scResLores.y; }

int Graphics::widthHires() const { return p->scRes.x; }

int Graphics::heightHires() const { return p->scRes.y; }

bool Graphics::isPingPongFramebufferActive() const {
    return p->screen.getPP().frontBuffer().fbo == FBO::boundFramebufferID || p->screen.getPP().backBuffer().fbo == FBO::boundFramebufferID;
}

int Graphics::displayContentWidth() const {
    return p->scSize.x;
}

int Graphics::displayContentHeight() const {
    return p->scSize.y;
}

int Graphics::displayWidth() const {
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(shState->sdlWindow()));
    return mode ? mode->w / p->backingScaleFactor : 0;
}

int Graphics::displayHeight() const {
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(shState->sdlWindow()));
    return mode ? mode->h / p->backingScaleFactor : 0;
}

void Graphics::resizeScreen(int width, int height) {
    p->threadData->rqWindowAdjust.wait(true);
    p->checkResize(true);
    
    Vec2i sizeLores(width, height);

    if (shState->config().enableHires) {
        double framebufferScalingFactor = shState->config().framebufferScalingFactor;
        width = (int)lround(framebufferScalingFactor * width);
        height = (int)lround(framebufferScalingFactor * height);
    }

    Vec2i size(width, height);
    
    if (p->scRes == size && p->scResLores == sizeLores)
        return;
    
    p->scRes = size;
    p->scResLores = sizeLores;
    
    p->screen.setResolution(width, height);
    
    if (p->integerScaleActive)
        p->rebuildIntegerScaleBuffer();
    
    TEXFBO::allocEmpty(p->frozenScene, width, height);
    
    FloatRect screenRect(0, 0, width, height);
    p->screenQuad.setTexPosRect(screenRect, screenRect);
    
    renderState.scissorBox.set(IntRect(0, 0, p->scRes.x, p->scRes.y));
    
    shState->eThread().requestWindowResize(width, height);
}

void Graphics::resizeWindow(int width, int height, bool center) {
    p->threadData->rqWindowAdjust.wait(true);
    p->checkResize();
    
    if (width == p->winSize.x / p->backingScaleFactor &&
        height == p->winSize.y / p->backingScaleFactor)
            return;

    shState->eThread().requestWindowResize(width, height);
    
    if (center)
        this->center();
}

void Graphics::screenshot(const char *filename) {
    p->threadData->rqWindowAdjust.wait(true);
    std::unique_ptr<Bitmap> ss(snapToBitmap());
    ss->saveToFile(filename);
    ss->dispose();
}

DEF_ATTR_RD_SIMPLE(Graphics, Brightness, int, p->brightness)

void Graphics::setBrightness(int value) {
    value = clamp(value, 0, 255);
    
    if (p->brightness == value)
        return;
    
    p->brightness = value;
    p->screen.setBrightness(value / 255.0);
}

void Graphics::reset() {
    /* Dispose all live Disposables */
    IntruListLink<Disposable> *iter;
    
    for (iter = p->dispList.begin(); iter != p->dispList.end();
         iter = iter->next) {
        iter->data->dispose();
    }
    
    p->dispList.clear();
    
    /* Reset attributes (frame count not included) */
    p->fpsLimiter.resetFrameAdjust();
    p->frozen = false;
    p->screen.getPP().clearBuffers();
    
    setFrameRate(DEF_FRAMERATE);
    setBrightness(255);
    
    // Always update at least once to clear the screen
    if (p->threadData->rqResetFinish)
        update();
    else
        repaintWait(p->threadData->rqResetFinish, false);
    p->threadData->rqReset = false;
}

void Graphics::center() {
    p->threadData->rqWindowAdjust.wait(true);
    if (getFullscreen())
        return;
    
    p->threadData->ethread->requestWindowCenter();
}

bool Graphics::getFullscreen() const {
    return p->threadData->ethread->getFullscreen();
}

void Graphics::setFullscreen(bool value) {
    p->threadData->ethread->requestFullscreenMode(value);
}

bool Graphics::getShowCursor() const {
    return p->threadData->ethread->getShowCursor();
}

void Graphics::setShowCursor(bool value) {
    p->threadData->ethread->requestShowCursor(value);
}

bool Graphics::getFixedAspectRatio() const
{
    // It's a bit hacky to expose config values as a Graphics
    // attribute, but there's really no point in state duplication
    return shState->config().fixedAspectRatio;
}

void Graphics::setFixedAspectRatio(bool value)
{
    shState->config().fixedAspectRatio = value;
    p->recalculateScreenSize(p->threadData->config.fixedAspectRatio);
    p->findHighestIntegerScale();
    p->recalculateScreenSize(p->threadData->config.fixedAspectRatio);
    p->updateScreenResoRatio(p->threadData);
}

int Graphics::getSmoothScaling() const
{
    // Same deal as with fixed aspect ratio
    return shState->config().smoothScaling;
}

void Graphics::setSmoothScaling(int value)
{
    shState->config().smoothScaling = value;
}

bool Graphics::getIntegerScaling() const
{
    return p->integerScaleActive;
}

void Graphics::setIntegerScaling(bool value)
{
    p->integerScaleActive = value;
    p->findHighestIntegerScale();
    p->rebuildIntegerScaleBuffer();
    
    p->recalculateScreenSize(p->threadData->config.fixedAspectRatio);
    p->updateScreenResoRatio(p->threadData);
}

bool Graphics::getLastMileScaling() const
{
    return p->integerLastMileScaling;
}

void Graphics::setLastMileScaling(bool value)
{
    p->integerLastMileScaling = value;
    p->recalculateScreenSize(p->threadData->config.fixedAspectRatio);
    p->updateScreenResoRatio(p->threadData);
}

bool Graphics::getThreadsafe() const
{
    return p->multithreadedMode;
}

void Graphics::setThreadsafe(bool value)
{
    p->multithreadedMode = value;
}

double Graphics::getScale() const {
    p->checkResize();
    return (double)(p->winSize.y / p->backingScaleFactor) / p->scRes.y;
    
}

void Graphics::setScale(double factor) {
    p->threadData->rqWindowAdjust.wait(true);
    factor = clamp(factor, 0.5, 4.0);
    
    if (factor == getScale())
        return;
    
    int widthpx = p->scRes.x * factor;
    int heightpx = p->scRes.y * factor;
    
    shState->eThread().requestWindowResize(widthpx, heightpx);
}

bool Graphics::getFrameskip() const { return p->useFrameSkip; }

void Graphics::setFrameskip(bool value) { p->useFrameSkip = value; }

Scene *Graphics::getScreen() const { return &p->screen; }

void Graphics::repaintWait(const std::atomic<bool> &exitCond, bool checkReset) {
    if (exitCond)
        return;
    
    // The SDL window thread owns the last presented texture. Repaint requests
    // never access an RGSS-thread RenderContext from a window callback.
    while (!exitCond) {
        shState->checkShutdown();
        if (checkReset) shState->checkReset();
        p->threadData->ethread->requestGpuPresent();
        p->fpsLimiter.delay();
        p->threadData->ethread->notifyFrame();
    }
}

void Graphics::lock(bool force) {
    p->setLock(force);
}

void Graphics::unlock(bool force) {
    p->releaseLock(force);
}

void Graphics::addDisposable(Disposable *d) { p->dispList.append(d->link); }

void Graphics::remDisposable(Disposable *d) { p->dispList.remove(d->link); }

#undef GRAPHICS_THREAD_LOCK
