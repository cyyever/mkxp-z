#include "binding-util.h"
#include "sharedstate.h"
#include "sdl-util.h"

#include <SDL3/SDL_init.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

IOTestState testState;
IOTestState *shState = &testState;
static int liveStreams = 0;
static int closedStreams = 0;
struct TestStream {
    std::string bytes;
    size_t offset = 0;
    size_t chunk = 1024;
    bool failRead = false;
    bool failClose = false;
    bool failSize = false;
    bool failSeek = false;
};

static SDL_IOStream *testStream(const char *kind) {
    auto *data = new TestStream;
    data->bytes = std::string("a\0\xffz", 4);
    if (!std::strcmp(kind, "@short")) data->chunk = 1;
    if (!std::strcmp(kind, "@error")) data->failRead = true;
    if (!std::strcmp(kind, "@closeerror")) data->failClose = true;
    if (!std::strcmp(kind, "@unknownsize")) data->failSize = true;
    if (!std::strcmp(kind, "@noseek")) data->failSeek = true;
    if (!std::strcmp(kind, "@empty")) data->bytes.clear();
    if (!std::strcmp(kind, "@badmarshal")) data->bytes = "bad";
    SDL_IOStreamInterface ioInterface;
    SDL_INIT_INTERFACE(&ioInterface);
    ioInterface.size = [](void *opaque) -> Sint64 {
        const auto &data = *static_cast<TestStream *>(opaque);
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
        if (data.failRead) {
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

static VALUE openFileInt(VALUE, VALUE kind) {
    StringValue(kind);
    VALUE klass = rb_const_get(rb_cObject, rb_intern("FileInt"));
    VALUE result = rb_obj_alloc(klass);
    setPrivateData(result, testStream(StringValueCStr(kind)));
    return result;
}
static VALUE live(VALUE) { return INT2NUM(liveStreams); }
static VALUE closed(VALUE) { return INT2NUM(closedStreams); }
static VALUE verifyBuffer(VALUE) {
    bool success = true;
    SDL_IOStream *source = testStream("@short");
    SDL_IOStream *borrowed = borrowIO(source);
    {
        SDLRWBuf<4, 8> buffer(borrowed);
        std::istream input(&buffer);
        char first = 0, second = 0;
        input.get(first);
        input.get(second);
        input.unget();
        input.unget();
        char firstAgain = 0, secondAgain = 0;
        input.get(firstAgain);
        input.get(secondAgain);
        std::string rest((std::istreambuf_iterator<char>(input)), {});
        success = first == 'a' && second == 0 && firstAgain == first && secondAgain == second
               && rest == std::string("\xffz", 2);
    }
    SDL_CloseIO(borrowed);
    success = success && liveStreams == 1; // Borrowed handle never owns source.
    SDL_SeekIO(source, 0, SDL_IO_SEEK_SET);
    Uint8 firstByte = 0;
    success = success && SDL_ReadU8(source, &firstByte) && firstByte == 97;
    // The source is still usable after its borrowed handle is closed.
    SDL_CloseIO(source);
    source = testStream("@error");
    {
        SDLRWBuf<> buffer(source);
        std::istream input(&buffer);
        char byte;
        input.get(byte);
        success = success && input.bad();
    }
    SDL_CloseIO(source);
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
    rb_define_global_function("fileint_test_open", RUBY_METHOD_FUNC(openFileInt), 1);
    rb_define_global_function("fileint_test_live", RUBY_METHOD_FUNC(live), 0);
    rb_define_global_function("fileint_test_closed", RUBY_METHOD_FUNC(closed), 0);
    rb_define_global_function("fileint_test_buffer", RUBY_METHOD_FUNC(verifyBuffer), 0);
    char noGems[] = "--disable-gems";
    std::vector<char *> args{argv[0], noGems};
    args.insert(args.end(), argv + 1, argv + argc);
    const int status = ruby_run_node(ruby_options(static_cast<int>(args.size()), args.data()));
    delete static_cast<RbData *>(testState.data);
    SDL_Quit();
    return status;
}
