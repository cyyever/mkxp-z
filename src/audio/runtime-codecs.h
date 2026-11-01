#ifndef MKXP_RUNTIME_CODECS_H
#define MKXP_RUNTIME_CODECS_H

#include <SDL3_sound/SDL_sound.h>

// Inspect the selected decoder, never the filename hint. CoreAudio's broad
// decoder starts with AIFF and must not bypass this policy on macOS.
inline bool isRuntimeAudio(const Sound_Sample *sample) {
    if (!sample || !sample->decoder || !sample->decoder->extensions ||
        !sample->decoder->extensions[0]) return false;
    const char *format = sample->decoder->extensions[0];
    return SDL_strcasecmp(format, "WAV") == 0 || SDL_strcasecmp(format, "FLAC") == 0;
}

#endif
