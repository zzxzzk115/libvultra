# Builtin Rendergraphs

## 2026-05-29

- Added embedded builtin rendergraph assets under `builtin/render/` and generated `builtin_rendergraphs.hpp`.
- Runtime builtin graphs are loaded from `builtin://render/*.vrg.json` through `AssetSystem::loadTextAssetSync`.
- Editor Render Graph window lists builtin graphs, loads them read-only, supports live apply, and exports through an explicit browse target.
- Builtin declarative renderer loading no longer auto-adds `res://shaders/project.vshaderlib.lua` or scans project graph passes.
- Scene instantiation now loads mesh default transforms synchronously so imported prefab submesh placement is applied deterministically.
- Model prefab import now writes mesh child `TransformComponent` values directly into generated `.vmanifest` files and bumps the importer to `model_prefab:7`.
- `vasset-example-vpk` no-arg mode now resolves the example asset root instead of accidentally using the engine repo `resources` folder.
- Builtin universal graphs now include `GeneralGaussianSplatComposite`, which pass-throughs regular scenes and runs gaussian preprocess/render when splats are present.
- Project `resources/render/default.vrg.json` is synced with builtin `universal` topology, with `Pixelate` present but disabled and `ToneMapping` disabled.
- `example-rayquery` no longer stores raw `GpuMesh*` pointers from `GpuResourcePool::meshes`; it stores mesh indices so vector growth during sync mesh loads cannot invalidate draw records.
- XR demo apps now enable a runtime camera override so ordinary example cameras, including manifest cameras without `XRViewComponent`, can request an XR session and cook XR eye cameras.
- XR mirror ImGui now includes renderer UI from XR cameras too, so runtime-override examples still show ImGui when no mono companion camera is cooked.
- `XRHeadset` now tracks begun frames and acquired swapchain images separately, so `shouldRender=false` frames end without releasing an image that was never acquired.
- `DemoAppHost` disables asset async loading for examples; AssetSystem async service calls become blocking sync loads when `asset.asyncLoading=false`, while editor/project runtime paths keep async loading by default.
- DemoAppHost XR runtime override cameras keep mono fallback enabled for examples; project-authored `XRViewComponent` defaults keep their existing mono fallback behavior.
- Declarative builtin `ShadowMap` / `DeferredLighting` now mirror the old DirectGBufferFeature light semantics: scenes with only non-directional lights do not inherit a hidden default directional light or shadow pass.
- XR runtime camera override now only starts the XR session and configures the DemoAppHost manual camera; it no longer forces every ECS/world camera to become an XR view.
- `XRHeadset` destructor now ends an in-flight OpenXR frame before ending the session/destroying swapchains, so exceptions during rendering do not leave an acquired frame behind.

Verification:
- `xmake build -y vultra-app`
- `xmake build -y example-demo-app`
- `xmake build -y example-sponza`
- `xmake build -y example-gltf-viewer`
- `xmake build -y example-gaussian-splatting`
- `xmake build -y vasset-example-vpk`
- `xmake run vasset-cli import resources --reimport`
- no-arg `build/windows/x64/release/vasset-example-vpk/vasset-example-vpk.exe`
- `xmake build -y example-rayquery`
- `xmake run example-rayquery` (stayed running until tool timeout instead of exiting/crashing)
- `xmake run example-gaussian-splatting`
- `xmake build -y example-openxr-triangle`
- `xmake build -y example-openxr-sponza`
- `xmake build -y example-openxr-gaussian-splatting`
- `xmake build -y vultra-app`
- `xmake build -y example-demo-app`
- `xmake build -y example-gaussian-splatting`
- `resources/render/default.vrg.json` JSON parse
- `xmake run vasset-cli import resources --reimport`
- `git diff --check` (only existing CRLF normalization warnings)
