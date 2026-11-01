#ifndef MKXP_SIGNAL_H
#define MKXP_SIGNAL_H

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// Engine notifications carry no arguments. Connections own their subscription;
// moving or destroying one disconnects the old receiver automatically.
struct SignalSlot {
    std::atomic<bool> connected{true};
    std::function<void()> callback;

    explicit SignalSlot(std::function<void()> callback)
        : callback(std::move(callback)) {}
};

class SignalConnection {
    std::weak_ptr<SignalSlot> slot;

public:
    SignalConnection() = default;
    explicit SignalConnection(const std::shared_ptr<SignalSlot> &slot) : slot(slot) {}
    SignalConnection(const SignalConnection &) = delete;
    SignalConnection &operator=(const SignalConnection &) = delete;
    SignalConnection(SignalConnection &&other) noexcept : slot(std::move(other.slot)) {}
    SignalConnection &operator=(SignalConnection &&other) noexcept {
        if (this != &other) {
            disconnect();
            slot = std::move(other.slot);
        }
        return *this;
    }
    ~SignalConnection() { disconnect(); }

    void disconnect() noexcept {
        if (auto current = slot.lock())
            current->connected = false;
        slot.reset();
    }
};

class Signal {
    std::mutex mutex;
    std::vector<std::shared_ptr<SignalSlot>> slots;

    void prune() {
        std::erase_if(slots, [](const auto &slot) { return !slot->connected.load(); });
    }

public:
    ~Signal() {
        for (const auto &slot : slots)
            slot->connected = false;
    }

    SignalConnection connect(std::function<void()> callback) {
        auto slot = std::make_shared<SignalSlot>(std::move(callback));
        std::lock_guard lock(mutex);
        prune();
        slots.push_back(slot);
        return SignalConnection(slot);
    }

    template<class Method, class Object>
    SignalConnection connect(Method method, Object *object) {
        return connect([method, object] { std::invoke(method, object); });
    }

    void operator()() {
        // Snapshot before callbacks: receivers may disconnect themselves or
        // others, connect new receivers, or emit recursively. New connections
        // are first called by the next emission.
        std::vector<std::shared_ptr<SignalSlot>> pending;
        {
            std::lock_guard lock(mutex);
            prune();
            pending = slots;
        }
        // As with other engine objects, receiver destruction and callback
        // execution must be serialized by the caller (the RGSS thread).
        for (const auto &slot : pending)
            if (slot->connected)
                slot->callback();
    }
};

#endif
