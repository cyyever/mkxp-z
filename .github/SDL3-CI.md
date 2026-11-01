# SDL3 CI

The SDL3 migration workflow builds the source versions and verifies the SHA-256
hashes recorded in the SDL3, SDL3_image, SDL3_ttf and FreeType wraps. It installs
a shared native SDK with SDL native PNG, optional stb JPEG and static FreeType.
JPEG is enabled in this API-test SDK for converter coverage; production runtime
images remain PNG-only. Other optional
image codecs and font-shaping dependencies are disabled for this focused suite.

The jobs compile and run the real SDL APIs used by `tests/sdl3` and the production
audio state machine in `tests/audio`. Linux also tests the production Ruby IO
bindings in `tests/io` with Ruby >= 4.0. These focused tests use dummy drivers
and do not validate SDL_GPU rendering. The full engine smoke and native GPU
pixel checks require a working GPU backend, such as Windows Direct3D 12.
An Xvfb/OpenGL-only setup does not validate the current renderer.

Hosted build labels are `ubuntu-26.04`, `windows-2025-vs2026` and `xcode-27`, as
listed in the [official runner images](https://github.com/actions/runner-images#available-images).
The Windows job uses native MSVC with native CMake/Ninja. The SDK jobs validate
APIs on their available build hosts; the Windows Server 2025 and macOS 26 hosts
do not establish application support for those OS releases. The application
macOS artifact targets macOS 27 on Apple Silicon.

FreeBSD tests run inside a VM pinned to `15.1`, the latest production RELEASE
when this workflow was added, verified against the
[FreeBSD release list](https://www.freebsd.org/releases/). Update that exact pin
when a newer RELEASE is published; older RELEASEs, STABLE and CURRENT are outside
the support policy. The VM runs both portable-core and focused SDL3 tests using
native Clang, following the [VM action's documented interface](https://github.com/vmactions/freebsd-vm).

To reproduce the SDK build locally, use Python >= 3.12, Meson >= 1.10, native
CMake and Ninja; Windows commands run in a Visual Studio developer shell:

```sh
python .github/scripts/build-sdl3-sdk.py
meson setup build/audio tests/audio --buildtype=release --cmake-prefix-path="$PWD/build/sdk"
meson setup build/sdl3 tests/sdl3 --buildtype=release --cmake-prefix-path="$PWD/build/sdk"
meson test -C build/audio --print-errorlogs
meson test -C build/sdl3 --print-errorlogs
```

Add `build/sdk/bin` to `PATH` for Windows DLL loading. Unix installations may
require `build/sdk/lib` in `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH`. The tests use
generated fixtures and bundled fonts, so they do not require game archives,
speakers or a GPU. Full-engine smoke is separate: see
[engine smoke instructions](../tests/sdl3/README.md) and
[native GPU checks](../tests/gpu/README.md).
