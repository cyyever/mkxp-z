# SDL_GPU renderer checks

These checks exercise the native device, resources, shaders, geometry and frame
encoder used by the production SDL_GPU renderer. They are separate from the
complete Ruby/SharedState engine smoke.

`gpu::Device` owns a device and its claimed window. Create and destroy it on the
window creation thread, after SDL video initialization and before destroying the
window. Destroy all Texture and CommandBuffer objects before their Device.
Command buffers stay on the thread that acquires them. Texture transfers and
offscreen commands may run on the RGSS worker; swapchain acquisition and final
presentation must run on the window thread. The bounded `FrameHandoff` transfers an owned Texture and completion Fence
between those threads. The producer submits through `submitWithFence()`, publishes
the result, and wakes the event loop; the consumer takes the frame without
blocking, waits its fence, and presents. `close()` drops queued frames and wakes
blocked producers before shutdown joins them. Callers must join producers before
destroying the mailbox. The production event loop uses this handoff. Swapchain
textures are borrowed/write-only.

Texture coordinates and tightly packed RGBA8 transfers use top-to-bottom rows.
Upload/download helpers wait on a fence; these are initial correctness paths,
not the intended batched transfer implementation for per-frame drawing. Pipeline
and uniform bindings use native SDL_GPU types, without a second graphics API.

The native test uses a real GPU and non-OpenGL SDL window. It verifies odd-sized
RGBA byte roundtrips, copy, ownership, worker readback, thread validation, fenced frame handoff/order, bounded-slot reuse, close waking
a blocked producer, command cancellation, and swapchain clear/submission. It passed on Windows MSVC/D3D12.
It needs an installed SDL3 CMake SDK:

```sh
meson setup build/gpu-tests tests/gpu
meson test -C build/gpu-tests --print-errorlogs
```

SDL API requirements:
- https://wiki.libsdl.org/SDL3/SDL_WaitAndAcquireGPUSwapchainTexture
- https://wiki.libsdl.org/SDL3/SDL_CancelGPUCommandBuffer
- https://wiki.libsdl.org/SDL3/CategoryGPU

## Quad pipeline pixels (Windows/D3D12)

`gpu-shader-pixels` uses offline FXC DXBC and the production `gpu-shader.cpp`
pipeline cache. All supplied fragment programs are rendered to an RGBA8 target and
read back: textured/flat/vertex colors, vertex/uniform alpha, normal/add/subtract/
kept-destination alpha, plane grayscale/tone/flash/opacity, masked/simple
transitions, bitmap source-over composition, and hue rotation. Expected channel
values are computed separately on the CPU; UNORM rounding tolerance is two bytes.
This validates these primitive effects, not full game/OpenGL pixel parity.
The Meson test depends on strict compilation of every HLSL entry point.

`QuadPipeline` accepts bytecode spans so the engine can embed the same offline
outputs without runtime compiler dependencies. It caches by program, blend and
render-target format; callers retain render-pass, buffer, viewport and scissor
ownership. The full vertex stride is 32 bytes; uniform blocks are 144 and 80 bytes.

Extended tests cover sprite normal/additive pattern blending with bush opacity,
animated autotile atlas frame selection, tilemap tint, flash/gray, KGL operations
and shadow cutouts. Nonuniform checkerboards verify horizontal three-tap blur and
identity Catmull-Rom/Lanczos sampling; constant color verifies filter normalization
and optional xBRZ. These checks do not yet compare xBRZ edges or whole scenes
against OpenGL. Use `-Dextra_shaders=true` in this standalone test project to
compile/test the separately licensed optional GPLv3+ shader.

## Batched offscreen frames

`gpu-frame-batches` compiles the production `FrameEncoder`. It verifies captured
per-draw viewport/scissor/blend/uniform state, explicit target LOAD/CLEAR behavior,
clear-only targets, recovery after a bind error inside a render pass, clipped-out
geometry, and twenty queued frames with different vertices/targets. Every queued
frame is read back to detect overwriting an in-flight vertex upload.

The encoder copies CPU geometry and draw state, uploads one contiguous vertex
buffer before rendering, precreates pipeline cache entries before opening passes,
and reuses cycling transfer/vertex buffers. `TextureView` is borrowed until
submission; offscreen target ownership stays with the caller. `submit()` returns
a completion fence and resets the recorded CPU commands. A render-pass guard
ends an active pass before exceptions cancel its command buffer. The production RenderContext and drawable adapters use this encoder.

## Production ShaderSet state adapter

The former runtime GLSL compiler and uniform-location code in engine `shader.cpp`
is replaced by state-only setters for the native HLSL programs. The adapter test
compiles exact production source/header copies in a private build subdirectory,
replacing only texture identity and the two RenderContext hooks. It checks setters
after bind, top-down/hires projection and Gray's exception, tiled/non-tiled pattern
UVs, bush orientation, masked/simple transition ordering, blur texel spacing,
Mitchell-Netravali parameters, autotile packing and KGL shadow state. Real GPU
resource lifetime and rendered effects are covered separately. The private test
headers cannot shadow production resource headers used by other targets.

Device textures also preserve configurable mip chains. Native tests read back
4x4->2x2->1x1 generated levels, verifying exact first-level blocks and terminal
average colors; invalid level counts/readback/generation are rejected.

## Production renderer integration pixels

`context-pixels.cpp` exercises actual ShaderSet, Quad, RenderContext, texture
resources and FrameEncoder together, without resource/context stubs. Its native
D3D12 checks cover top-down checkerboard UVs, preserving queued pixels before
source-image replacement, disabling a disposed sprite pattern, and submitting
pending geometry before deleting its sampled texture. It does not instantiate
the complete engine SharedState/Ruby runtime; the engine smoke test covers that
boundary with the full engine build.
