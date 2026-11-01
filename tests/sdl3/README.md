# SDL3 migration checks

Build the standalone API tests against an installed SDL3, SDL3_image and
SDL3_ttf SDK. Set `CMAKE_PREFIX_PATH` to its install directory:

```sh
meson setup build/sdl3-tests tests/sdl3
meson test -C build/sdl3-tests --print-errorlogs
```

These use the dummy video/audio drivers and test real codecs, pixel conversion,
scaled blits, opaque/borrowed IO ownership and EOF, font measurement/rendering,
window resizing and user events. `tests/audio` covers streaming playback states.
`tests/io` additionally checks the production Ruby IO bindings and Marshal cleanup.

After a full engine build, run the integration smoke with a working OpenGL
display (Linux CI uses Mesa software rendering and Xvfb):

```sh
xvfb-run -a python3 tests/sdl3/engine-smoke.py build/mkxp-z.x86_64
```

On Windows, use the same script with `build-msvc/mkxp-z.x86_64.exe` from a desktop
session. It uses isolated temporary files and bundled assets, and checks image
loading/saving with the path cache enabled, font cloning/rendering and GC, JSON
settings, Windows function pointers, an 800x600 resize and screenshot disposal.
No game archives or speakers are required. It is not a gameplay test.
