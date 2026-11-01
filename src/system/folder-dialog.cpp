#include "system.h"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_properties.h>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace {
struct Selection {
    SDL_PropertiesID properties = SDL_CreateProperties();
    std::mutex mutex;
    std::condition_variable ready;
    bool complete = false;
    std::string path, error;
    std::exception_ptr failure;
    ~Selection() { if (properties) SDL_DestroyProperties(properties); }
};

void SDLCALL selected(void *userdata, const char *const *files, int) noexcept {
    // Both caller and callback own the result. Publishing completion must not
    // destroy callback data while another thread is still notifying the caller.
    std::unique_ptr<std::shared_ptr<Selection>> owner(
        static_cast<std::shared_ptr<Selection> *>(userdata));
    auto result = *owner;
    {
        std::lock_guard lock(result->mutex);
        try {
            if (!files) result->error = SDL_GetError(); // Error is callback-thread-local.
            else if (files[0]) result->path = files[0]; // SDL owns these UTF8 bytes.
            if (!files && result->error.empty()) result->error = "Folder selection failed";
        } catch (...) {
            result->failure = std::current_exception();
        }
        result->complete = true;
    }
    result->ready.notify_one();
}
}

std::string systemImpl::selectPath(SDL_Window *window, const char *title, const char *accept) {
    if (!SDL_IsMainThread()) throw std::logic_error("Folder selection must run on the main thread");
    auto result = std::make_shared<Selection>();
    if (!result->properties ||
        !SDL_SetPointerProperty(result->properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window) ||
        !SDL_SetBooleanProperty(result->properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false) ||
        (title && !SDL_SetStringProperty(result->properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, title)) ||
        (accept && !SDL_SetStringProperty(result->properties, SDL_PROP_FILE_DIALOG_ACCEPT_STRING, accept)))
        throw std::runtime_error(SDL_GetError());

    auto owner = std::make_unique<std::shared_ptr<Selection>>(result);
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, selected, owner.release(), result->properties);
    for (;;) {
        // Cocoa sheets need the startup thread to process native events. Do not
        // consume queued SDL events that the engine will handle after startup.
        SDL_PumpEvents();
        std::unique_lock lock(result->mutex);
        if (result->ready.wait_for(lock, std::chrono::milliseconds(16), [&] { return result->complete; })) break;
    }
    if (result->failure) std::rethrow_exception(result->failure);
    if (!result->error.empty()) throw std::runtime_error(result->error);
    return result->path;
}
