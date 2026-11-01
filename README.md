# mkxp-z

Supported desktop targets are Windows 11 releases from 2026 onward, macOS 27
and newer on Apple silicon, Linux distributions released from 2026 onward,
and the latest production FreeBSD RELEASE only. Mobile platforms are not
supported. Shared filesystem/thread/timing code uses standard C++; native
platform code remains where desktop APIs differ. CI build hosts may be older
than deployment targets; a successful cross-build is not a runtime OS test.

This personal fork builds the engine in **C++26** mode with current compilers
and Meson 1.10 or newer. GCC and Clang use `c++26` (or the equivalent `c++2c`
flag chosen by Meson); MSVC uses `/std:c++latest`. This selects the language
mode, without requiring every proposed C++26 feature to be implemented.
See the [MSVC language-mode documentation](https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version)
and [Clang implementation status](https://clang.llvm.org/cxx_status.html).

Portable filesystem operations use `std::filesystem` with UTF-8 paths on all
platforms. macOS retains its native app-bundle lookup and folder picker.
Filesystem calls use the STL directly; `util/paths.h` only handles UTF-8
boundaries, RGSS separator normalization and reading file contents. Assets are
loaded from unpacked folders, searched in order: patches, the game folder, then
RTP folders. The path cache provides case-insensitive game asset lookup.
Extract RGSSAD and ZIP archives before configuring resource folders.
RGSS serialization uses `std::endian`, `std::byteswap` and `std::bit_cast`,
preserving the existing little-endian format. Third-party libraries retain
their own language requirements.

Game/configuration text and `String#to_utf8` accept UTF-8 only; other encodings
must be converted before loading. A UTF-8 BOM is stripped and malformed UTF-8
is rejected. Font metadata has a separate decoder for its required big-endian
UTF-16/32 name records. No charset detection or iconv dependency is required.

Render and disposal notifications use scoped connections: destroying or
reassigning a connection disconnects its receiver. Callbacks can disconnect
receivers or add subscriptions during emission; new subscriptions run from the
next emission. Receiver destruction and callbacks run on the RGSS thread.

The portable core can be built and tested without SDL, Ruby or game assets:

```sh
meson setup build/core -Dcore_tests_only=true --buildtype=release
meson test -C build/core --print-errorlogs
```

Run these commands from a Visual Studio developer shell when using MSVC.
The core test workflow covers Linux GCC/Clang, macOS C++/Objective-C++, and
Windows MSVC. These tests cover the production filesystem, serialization,
notification lifetimes and text encoding selection; they do not test game rendering.
Windows engine builds require a native MSVC-compatible toolchain. MinGW cross
compilation is unsupported. Native MSVC engine builds and rendering smoke tests
have passed locally. Hosted native API checks and full renderer validation are
separate; see [SDL3 CI](.github/SDL3-CI.md).

<p align="center"><b>
  <a href="https://github.com/mkxp-z/mkxp-z/actions/workflows/autobuild.yml?query=event%3Apush">Automatic Builds</a>
  ・
  <a href="https://github.com/mkxp-z/mkxp-z/wiki">Documentation</a>
  ・
  <a href="https://matrix.to/#/#rpgmaker:mapleshrine.eu">Matrix Space</a>
  ・
  <a href="https://discord.gg/A8xHE8P">Discord server</a>
</b></p>

<p align=center>
    <img src="screenshot.png?raw=true" width=512 height=412>
</p>

This fork runs RPG Maker game scripts with embedded MRI Ruby and SDL3. The
renderer uses SDL_GPU resources, offline shaders and native presentation;
Windows builds use Direct3D 12. The previous OpenGL/ANGLE renderer is removed.
The full native engine build has been validated with Windows MSVC/Direct3D 12;
non-Windows shader embedding and full-engine integration remain unfinished.
The desktop target policy above does not imply those renderer paths are ready.
Existing RGSS logical coordinates, high-resolution bitmap APIs and scaling
options remain available independently of the physical window size.

Runtime images use PNG and audio uses WAV/FLAC. The optional
[asset converter](tools/asset-convert/README.md) can rebuild legacy assets before
launch; it does not recover information lost by lossy source compression.

mkxp-z is licensed under the GNU General Public License v2+.

## Bindings
Bindings provide the glue code for an interpreted language environment to run game scripts in. mkxp-z focuses on MRI and as such the mruby and null bindings are not included.

## Fonts

In the RMXP version of RGSS, fonts are loaded directly from system specific search paths (meaning they must be installed to be available to games). Because this whole thing is a giant platform-dependent headache, Ancurio decided to implement the behavior Enterbrain thankfully added in VX Ace: loading fonts will automatically search a folder called "Fonts", which obeys the default searchpath behavior (ie. it can be located directly in the game folder, or an RTP).

If a requested font is not found, no error is generated. Instead, a built-in font is used. By default, this font is Liberation Sans.

## What doesn't work

* wma audio files
* Creating Bitmaps with sizes greater than your hardware's texture size limit.
  * There is an exception to this, called *mega surface*. When a Bitmap bigger than the texture limit is created from a file, it is not stored in VRAM, but regular RAM. Its sole purpose is to be used as a tileset bitmap. Any other operation to it (besides blitting to a regular Bitmap) will result in an error.
 
## Notable Thanks

+ Ancurio, who wrote mkxp in the first place
+ Savordez and Aeodyn for making stuff work on Windows
+ Eblo for the `Graphics.play_movie` implementation
+ basically anyone else with commits in here or that reported problems to me
