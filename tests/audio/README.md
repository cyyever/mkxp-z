# Audio tests

These tests compile the production audio code against SDL3 and SDL3_mixer and
play through an offline mixer (`MIX_CreateMixer` + `MIX_Generate`), so no
speakers, audio driver or game assets are needed and levels and positions are
deterministic. The fixtures in `fixtures/` are generated one-second 440 Hz
tones in every runtime format (WAV u8, A-law WAV, FLAC, MP3, Ogg Vorbis, and an
Ogg with `LOOPSTART`/`LOOPLENGTH` tags).

With SDL3 >= 3.4 and SDL3_mixer >= 3.2 installed with their CMake packages
discoverable through `CMAKE_PREFIX_PATH` (`.github/scripts/build-sdl3-sdk.py`
builds them):

```sh
meson setup build/audio-tests tests/audio
meson test -C build/audio-tests --print-errorlogs
```

- `mixer-decode`: the decoder set, each format's decoder and length, loop tags
  ignored when requested (needs the patched stb_vorbis decoder).
- `sound-emitter`: `SE.sourceCount`, source eviction/reuse order, `se_stop`,
  the RGSS volume curve, pitch, every codec, error types, cache hits and
  eviction while cached audio is still playing.
- `audio-stream`: one BGM/BGS/ME track: finite end, offsets, pause/resume,
  stop, offsets past the end, gapless looping, pitch, volume, every codec
  looping over the whole file (loop tags ignored), error types.
- `audio-semantics`: the `Audio` class with its fade and ME threads in real
  time: in-place volume update, restart on pitch change, position fade-in,
  `bgm_fade`, ME pausing and resuming the BGM with its fade-in, BGM stopped or
  started during an ME, `me_fade`, BGS, several BGM tracks, `__reset__`,
  shutdown while playing.
