#include "binding-util.h"
#include "sharedstate.h"
#include "sdl-util.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

IOTestState testState;
IOTestState *shState = &testState;
static std::atomic<int> liveStreams{0};
static std::atomic<int> closedStreams{0};
struct TestStream {
    std::string bytes;
    size_t offset = 0;
    size_t chunk = 1024;
    bool failRead = false;
    bool slowRead = false;
    bool oversized = false;
    bool failReadAfterChunk = false;
    bool failClose = false;
    bool failSize = false;
    bool failSeek = false;
};

static SDL_IOStream *testStream(const char *kind) {
    auto *data = new TestStream;
    data->bytes = std::string("a\0\xffz", 4);
    if (!std::strcmp(kind, "@short")) data->chunk = 1;
    if (!std::strcmp(kind, "@slow")) { data->chunk = 1; data->slowRead = true; }
    if (!std::strcmp(kind, "@oversize")) data->oversized = true;
    if (!std::strcmp(kind, "@error")) data->failRead = true;
    if (!std::strcmp(kind, "@partialerror")) { data->failReadAfterChunk = true; data->chunk = 1; }
    if (!std::strcmp(kind, "@closeerror")) data->failClose = true;
    if (!std::strcmp(kind, "@unknownsize")) data->failSize = true;
    if (!std::strcmp(kind, "@noseek")) data->failSeek = true;
    if (!std::strcmp(kind, "@empty")) data->bytes.clear();
    if (!std::strcmp(kind, "@badmarshal")) data->bytes = "bad";
    SDL_IOStreamInterface ioInterface;
    SDL_INIT_INTERFACE(&ioInterface);
    ioInterface.size = [](void *opaque) -> Sint64 {
        const auto &data = *static_cast<TestStream *>(opaque);
        if (data.oversized) return static_cast<Sint64>((std::numeric_limits<long>::max)()) + 1;
        if (data.failSize) { SDL_SetError("injected size failure"); return -1; }
        return data.bytes.size();
    };
    ioInterface.seek = [](void *opaque, Sint64 offset, SDL_IOWhence whence) -> Sint64 {
        auto &data = *static_cast<TestStream *>(opaque);
        if (data.failSeek) { SDL_SetError("injected seek failure"); return -1; }
        const Sint64 base = whence == SDL_IO_SEEK_SET ? 0 :
            whence == SDL_IO_SEEK_CUR ? data.offset : data.bytes.size();
        if (base + offset < 0) return -1;
        data.offset = static_cast<size_t>(base + offset);
        return data.offset;
    };
    ioInterface.read = [](void *opaque, void *buffer, size_t requested, SDL_IOStatus *status) -> size_t {
        auto &data = *static_cast<TestStream *>(opaque);
        if (data.slowRead) SDL_Delay(10);
        if (data.failRead || (data.failReadAfterChunk && data.offset > 0)) {
            SDL_SetError("injected read failure");
            *status = SDL_IO_STATUS_ERROR;
            return 0;
        }
        const size_t remaining = data.bytes.size() > data.offset ? data.bytes.size() - data.offset : 0;
        const size_t count = std::min<size_t>({requested, remaining, data.chunk});
        std::memcpy(buffer, data.bytes.data() + (std::min)(data.offset, data.bytes.size()), count);
        data.offset += count;
        *status = data.offset == data.bytes.size() ? SDL_IO_STATUS_EOF : SDL_IO_STATUS_READY;
        return count;
    };
    ioInterface.close = [](void *opaque) -> bool {
        auto *data = static_cast<TestStream *>(opaque);
        const bool success = !data->failClose;
        delete data;
        --liveStreams;
        ++closedStreams;
        if (!success) SDL_SetError("injected close failure");
        return success;
    };
    SDL_IOStream *stream = SDL_OpenIO(&ioInterface, data);
    if (!stream) { delete data; return nullptr; }
    ++liveStreams;
    return stream;
}

SDL_IOStream *FileSystem::openReadRaw(const char *filename) {
    auto *stream = filename[0] == '@' ? testStream(filename) : SDL_IOFromFile(filename, "rb");
    if (!stream) throw Exception(Exception::NoFileError, "%s", SDL_GetError());
    return stream;
}

static VALUE live(VALUE) { return INT2NUM(liveStreams); }
static VALUE closed(VALUE) { return INT2NUM(closedStreams); }
static VALUE verifyLoad(VALUE) {
    const int liveBefore = liveStreams, closedBefore = closedStreams;
    bool success;
    {
        std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> source(testStream("@short"), SDL_CloseIO);
        std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> borrowed(borrowIO(source.get()), SDL_CloseIO);
        std::string bytes;
        success = readFileSDL(borrowed.get(), bytes) && bytes == std::string("a\0\xffz", 4);
        borrowed.reset();
        success = success && liveStreams == liveBefore + 1 && closedStreams == closedBefore;
        Uint8 firstByte = 0;
        success = success && SDL_SeekIO(source.get(), 0, SDL_IO_SEEK_SET) == 0
                  && SDL_ReadU8(source.get(), &firstByte) && firstByte == 97;
    }
    success = success && liveStreams == liveBefore && closedStreams == closedBefore + 1;
    for (const char *kind : {"@error", "@partialerror"}) {
        std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> source(testStream(kind), SDL_CloseIO);
        std::string bytes = "unchanged";
        success = success && !readFileSDL(source.get(), bytes) && bytes == "unchanged";
    }
    success = success && liveStreams == liveBefore && closedStreams == closedBefore + 3;
    return success ? Qtrue : Qfalse;
}

void fileIntBindingInit();
int main(int argc, char **argv) {
    ruby_sysinit(&argc, &argv);
    RUBY_INIT_STACK;
    ruby_init();
    SDL_Init(0);
    testState.data = new RbData;
    fileIntBindingInit();
    rb_define_global_function("fileint_test_live", RUBY_METHOD_FUNC(live), 0);
    rb_define_global_function("fileint_test_closed", RUBY_METHOD_FUNC(closed), 0);
    rb_define_global_function("fileint_test_load", RUBY_METHOD_FUNC(verifyLoad), 0);
    char noGems[] = "--disable-gems";
    std::vector<char *> args{argv[0], noGems};
    args.insert(args.end(), argv + 1, argv + argc);
    const int status = ruby_run_node(ruby_options(static_cast<int>(args.size()), args.data()));
    delete static_cast<RbData *>(testState.data);
    SDL_Quit();
    return status;
}
