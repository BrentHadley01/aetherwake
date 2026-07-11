# Aetherwake native engine

Aetherwake stays local and engine-owned. CMake fetches every open-source runtime dependency to `build/_deps`; game rules, content formats, networking authority, and source assets live in this repository.

## Local stack

| Need | Local component | Purpose |
| --- | --- | --- |
| Window, input, controller | SDL3 | Platform layer |
| Rendering math | GLM | Camera, transforms, lighting math |
| Game state | EnTT | Explicit entity/component systems |
| Physics | Jolt Physics | Character collision and rigid bodies |
| Network transport | ENet | Low-latency transport behind host authority |
| Content/save data | nlohmann/json | Authored data and versioned saves |
| Debug tools | Dear ImGui | Live inspector and multiplayer tools |

## Bootstrap

```powershell
.\scripts\bootstrap.ps1
.\scripts\bootstrap.ps1 -Build
```

The script uses CMake from PATH or `C:\Program Files\CMake\bin\cmake.exe`. The first configure downloads dependencies under `build/_deps`, which is local to this checkout and ignored by Git. A Visual Studio C++ toolchain is required to compile on Windows.

## Native roadmap

1. SDL3 window/input and renderer loop; OpenGL first, Vulkan only after profiling proves the need.
2. EnTT world, scene-streaming cells, resource handles, and JSON-authored content.
3. Jolt character controller, raycasts, overlap queries, and replicated interactable props.
4. PBR renderer: glTF import, texture streaming, shadows, HDR/tone mapping, volumetrics, and GPU profiling.
5. Animation graph, audio mixer, particles, and ImGui developer tools.
6. ENet host session, prediction/reconciliation, interest management, reconnects, and soak tests.
7. Steamworks adapter only from the Steamworks SDK supplied to the project owner; no proprietary SDK binaries are committed.

Hyperrealism comes from assets, animation, lighting, post-processing, and performance work—not a remote service or proprietary editor. This modular setup preserves local control while avoiding a full engine rewrite for each system.
