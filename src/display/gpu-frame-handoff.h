#pragma once

#include "gpu-device.h"
#include <condition_variable>
#include <mutex>
#include <optional>

namespace gpu {
struct CompletedFrame {
    Texture texture;
    Fence ready;
    Uint64 sequence;
};

// RGSS produces offscreen frames; SDL's window thread consumes and presents.
// The bounded slot applies backpressure without blocking the event thread.
// The Device must outlive the mailbox and any frames taken from it.
class FrameHandoff {
public:
    ~FrameHandoff() { close(); }
    FrameHandoff() = default;
    FrameHandoff(const FrameHandoff &) = delete;
    FrameHandoff &operator=(const FrameHandoff &) = delete;

    // Returns false after close. On success the caller should wake the SDL
    // event thread. No pass may still be open on the submitted render command.
    bool publish(CompletedFrame frame) {
        std::unique_lock lock(mutex);
        available.wait(lock, [&] { return closed || !pending; });
        if (closed) return false;
        pending.emplace(std::move(frame));
        return true;
    }

    // Event-thread polling must not block. Wait on the returned ready fence
    // before using the texture, then submit its blit to the acquired swapchain.
    std::optional<CompletedFrame> tryTake() {
        std::unique_lock lock(mutex);
        if (!pending) return std::nullopt;
        auto result = std::move(pending);
        pending.reset();
        lock.unlock();
        available.notify_one();
        return result;
    }

    // Call before joining the producer during shutdown/reset. The producer
    // must finish before destroying this mailbox (including a blocked publish).
    void close() {
        std::unique_lock lock(mutex);
        closed = true;
        pending.reset();
        lock.unlock();
        available.notify_all();
    }
private:
    std::mutex mutex;
    std::condition_variable available;
    std::optional<CompletedFrame> pending;
    bool closed = false;
};
}
