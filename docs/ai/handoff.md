# Vultra Handoff

## Current baseline

- The public `vultra` static library keeps direct VRI, explicit RenderGraph and `GpuScene(Device&, SceneData)` access available to C++ research programs. Source is grouped under `core`, `platform`, `drivers`, `assets`, `servers`, `scene`, `ui`, `main` and `api`. `vultra-vgui` and `vultra-scripting` are opt-in static targets.
- `RuntimeContext` owns Window, Device, Swapchain/Frame and RenderingServer in dependency order. A `GpuSceneHandle` owns a context-checked RID; graph resources, runtime `ObjectId`s and persistent asset/node IDs are distinct. The packaged renderer imports all static mesh nodes, applies parent transforms and bakes one GPU scene at startup. Live node transforms are not yet reflected in that GPU scene.
- `EditorGui` wraps ImGui for tools and debug panels. `VGui` wraps RmlUi with embedded PNG control styles for in-game UI; advanced RmlUi masks, transforms, layers and effects remain unsupported. Research exposes the renderer settings, graph observer and intermediate texture previews. Examples remain separate targets grouped by feature.
- `vultra-pack` creates a version-1 project VPK and can append it to a copy of `vultra-runtime`. The runtime embeds a separate built-in shader VPK, so the packaged Linux player launches without xmake. Both archives currently extract to temporary files before import. Lua is linked statically; native extension libraries load from files, and C# uses an installed .NET 10 runtime.
- One versioned C ABI serves native extensions, C++ scripts, Lua and C# node scripts. `example-scripting` shows the C++ ship controller, Lua pickup logic and safe Godot-style C# `Node3D` script in one rendered scene. Reload stages replacements before retiring callbacks, preserves the old module on load failure, and does not transfer script-local state. The generated C# ABI layouts are internal; public C# node wrappers, property metadata and script discovery are not generated yet.
- Pre-release serialized formats remain version 1. Change them directly when needed; no migration or backward compatibility layer is required before release. Run `xmake codegen --check` when annotated APIs change.

## Linux verification

- The default build and `xmake build --all` pass locally with the current SDL3 configuration. `xmake codegen --check`, project C/C++ `clang-format --dry-run --Werror`, and focused `clang-tidy` on script hot reload and RuntimeContext pass.
- `test-script-host`, `test-vpk`, `test-scene` and the managed-control test pass. `example-scripting` completed a fresh 30-frame Workspace 5 run without script or GPU errors. Earlier captures include a 60-frame scripting example and a VPK with a C# script running for 30 frames. Earlier finite-frame UI, scene, ray, XR desktop-mirror and Research runs include image readbacks; they do not validate a physical headset.
- `VULTRA_WINDOW_SYSTEM=wayland xmake test -v -j1` on Workspace 5 passed 18/20 tests. `test-desktop` failed its requested-resize notification assertion and `test-display` failed platform viewport creation/resize under Hyprland; all other tests, including GPU readback, glTF, scene, script hot reload and VPK, passed. The X11 suite needs a window manager that honors requested resize, detached viewports and minimization. Do not weaken those assertions to satisfy this desktop session.
- Windows x64, clean-target Linux delivery, physical XR hardware and interactive Wayland file picking have not been validated.

## Next: Windows x64

1. On a clean Windows x64 machine with Visual Studio 2022, Vulkan SDK, xmake and .NET 10 SDK, build `xmake build -y --all`. Validate GLFW first, then configure `--libvultra_window_backend=sdl3` and build again. Resolve any static Slang, local VRI patch, BC7, RmlUi, OpenXR or Win32 resource build/link issues in their owning targets.
2. Run `xmake test -v` in a normal Windows desktop session. Run finite-frame `example-basics`, `example-scene`, `example-ui`, `example-research`, `example-ray` and `example-scripting` modes with captures, then inspect images and GPU diagnostics. Exercise XR only when an OpenXR runtime and headset are available.
3. Pack `resources/research.vproject`, append the VPK with `vultra-pack --embed`, and run the copied `.exe` from a clean directory without xmake. Check built-in VPK loading through `runtime/src/builtin_pack.rc`, package integrity failures, Win32 DLL dependencies and both external and embedded project modes. Test native plugin loading and .NET 10 script startup/reload separately.
4. Keep Windows support marked unverified until the build, tests and packaged EXE run on Windows. After that, prioritize live scene-to-server updates and broader generated scripting bindings; these are current architecture gaps, not Win64 prerequisites.

Keep temporary captures and logs under ignored `build/.tmp/`. Use Workspace 5 for visible Linux test runs.
