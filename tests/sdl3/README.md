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

After a full engine build, run the integration smoke from a desktop session
with a working SDL_GPU backend (the Windows build uses Direct3D 12):

```powershell
python tests/sdl3/engine-smoke.py build-msvc/mkxp-z.x86_64.exe
```

The executable path is the output of your full engine build. Dummy video and
an Xvfb/OpenGL-only environment are insufficient for this renderer.
The smoke uses isolated temporary files and bundled assets, and checks image
loading/saving with the path cache enabled, font cloning/rendering and GC, JSON
settings, Windows function pointers, an 800x600 resize and screenshot disposal.
No game archives or speakers are required. It is not a gameplay test.
