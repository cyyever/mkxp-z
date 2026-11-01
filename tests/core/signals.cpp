#include "util/engine-signal.h"

#include <memory>
#include <stdexcept>

void require(bool condition) {
    if (!condition)
        throw std::runtime_error("signal lifetime or emission order failure");
}

int main() {
    Signal signal;
    int calls = 0;
    {
        auto connection = signal.connect([&] { ++calls; });
        signal();
        require(calls == 1);
        auto moved = std::move(connection);
        connection.disconnect();
        signal();
        require(calls == 2);
    }
    signal();
    require(calls == 2);

    SignalConnection later;
    auto first = signal.connect([&] { later.disconnect(); });
    later = signal.connect([&] { ++calls; });
    signal();
    require(calls == 2);

    SignalConnection added;
    auto adding = signal.connect([&] {
        if (calls == 2)
            added = signal.connect([&] { ++calls; });
    });
    signal();
    require(calls == 2);
    adding.disconnect();
    signal();
    require(calls == 3);

    Signal recursive;
    SignalConnection self;
    self = recursive.connect([&] {
        self.disconnect();
        recursive();
        ++calls;
    });
    recursive();
    require(calls == 4);

    auto emitter = std::make_unique<Signal>();
    auto destroying = emitter->connect([&] { emitter.reset(); });
    auto skipped = emitter->connect([&] { ++calls; });
    (*emitter)();
    require(calls == 4);
    destroying.disconnect();
    skipped.disconnect();

    Signal reassigned;
    auto owned = reassigned.connect([&] { ++calls; });
    owned = reassigned.connect([&] { calls += 2; });
    reassigned();
    require(calls == 6);

    struct Receiver {
        int calls = 0;
        void receive() { ++calls; }
    } receiver;
    auto member = reassigned.connect(&Receiver::receive, &receiver);
    reassigned();
    require(receiver.calls == 1);
}
