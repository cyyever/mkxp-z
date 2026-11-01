# Ruby/SDL file IO tests

This standalone test project compiles the production file bindings and SDL stream
adapter against Ruby and SDL3. A small asset-lookup fixture supplies real SDL IO
streams with short reads and injected failures; it does not replace Ruby Marshal
or SDL's stream implementation.

With Ruby >= 3.4 development headers/libraries visible to pkg-config and the SDL3 >= 3.4
CMake package discoverable through `CMAKE_PREFIX_PATH`:

```sh
meson setup build/io-tests tests/io
meson test -C build/io-tests --print-errorlogs
```

The checks cover single ownership on close/finalization, EOF and zero-length
reads, binary bytes, invalid lengths, read/size/seek/close failures, guaranteed
close after Marshal load/dump exceptions, and custom Marshal binary payloads.
They also exercise short streambuf reads with putback, IO error propagation,
and borrowed-stream ownership.
