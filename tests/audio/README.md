# SDL audio stream tests

These tests compile the production streaming state machine against SDL3 and use
SDL's dummy playback driver, so no speakers or game assets are required. Small
asset/decoder/suspend test doubles generate PCM while SDL performs the actual
queueing, conversion, resampling and playback.

With SDL3 >= 3.4 installed with its CMake package discoverable through `CMAKE_PREFIX_PATH`:

```sh
meson setup build/audio-tests tests/audio
meson test -C build/audio-tests --print-errorlogs
```

Coverage includes finite EOF, replay and seeking, mono unsigned 8-bit conversion,
loop offsets, gain/pitch setup, pause/resume, stop/restart, destruction with an
active worker, empty loop sources, and device binding failure. Additional checks
cover closed control calls, finite EOF while paused, resume preserving queued
audio despite an offset argument, seeks beyond EOF, stop while paused, active
source replacement across sample formats, and idempotent close without leaks. Codec decoding,
application suspend events and audible game playback require engine integration
checks in addition to these isolated tests.
