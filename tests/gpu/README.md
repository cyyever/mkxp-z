# SDL GPU device foundation

This is the first resource layer for the renderer migration. It is not yet an
engine backend and does not replace or change the default OpenGL renderer.

`gpu::Device` owns a device and its claimed window. Create and destroy it on the
window creation thread, after SDL video initialization and before destroying the
window. Destroy all Texture and CommandBuffer objects before their Device.
Command buffers stay on the thread that acquires them. Texture transfers and
offscreen commands may run on the RGSS worker; swapchain acquisition and final
presentation must run on the window thread. The renderer will need a texture and
fence handoff between those threads. Swapchain textures are borrowed/write-only.

Texture coordinates and tightly packed RGBA8 transfers use top-to-bottom rows.
Upload/download helpers wait on a fence; these are initial correctness paths,
not the intended batched transfer implementation for per-frame drawing. Pipeline
and uniform bindings use native SDL_GPU types, without a second graphics API.

The native test uses a real GPU and non-OpenGL SDL window. It verifies odd-sized
RGBA byte roundtrips, copy, ownership, worker readback, thread validation, command
cancellation, and swapchain clear/submission. It passed on Windows MSVC/D3D12.
It needs an installed SDL3 CMake SDK:

```sh
meson setup build/gpu-tests tests/gpu
meson test -C build/gpu-tests --print-errorlogs
```

SDL API requirements:
- https://wiki.libsdl.org/SDL3/SDL_WaitAndAcquireGPUSwapchainTexture
- https://wiki.libsdl.org/SDL3/SDL_CancelGPUCommandBuffer
- https://wiki.libsdl.org/SDL3/CategoryGPU
