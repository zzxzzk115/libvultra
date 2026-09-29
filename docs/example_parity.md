# Example Coverage Against `dev`

The reference is libvultra's old `dev` branch, not `dev-next`. This table separates example coverage from renderer feature parity. All available examples are default xmake run targets. Unsupported device features produce an explicit creation error; the examples do not substitute software rendering.

| `dev` example | `dev-VRI` target | Current implementation |
| --- | --- | --- |
| Window | `example-window` | Same authored clear color through an sRGB swapchain |
| RHI triangle | `example-rhi-triangle` | Indexed VRI draw, black background, sRGB swapchain |
| FrameGraph triangle | `example-rendergraph-triangle` | Vultra's own code-driven graph; no external fg dependency |
| ImGui | `example-imgui` | Docking, native viewports, render-target preview, theme selection and PNG capture |
| Debug Draw | `example-debugdraw` | Damaged Helmet with the original HDR, actual world-space model bounds, grid, axes and sphere wireframes |
| glTF Viewer | `example-gltf-viewer` | Damaged Helmet, model selection, OpenPBR subset, IBL and CSM/PCF/PCSS |
| Sponza | `example-sponza` | Original glTF, buffers, textures and HDR; first-person camera; built-in forward passes |
| Sponza meshlet path | `example-meshshading-sponza` | `dev` meshoptimizer clusterization, task-stage frustum culling, mesh drawing, meshlet colors and an indexed comparison switch |
| Ray Query | `example-rayquery` | Original Armadillo/floor scene; rasterization plus fragment-stage hardware shadow queries |
| Ray Tracing Triangle | `example-raytracing-triangle` | Hardware raygen, miss, closest-hit and shader binding table |
| Ray Tracing Cornell Box | `example-raytracing-cornell-box` | Original OBJ/MTL; primary rays and secondary shadow rays to the emissive quad centroid |
| Mesh Shading Triangle | `example-meshshading-triangle` | Slang task/amplification, mesh and fragment stages; same depth-offset triangles |
| OpenXR Triangle | `example-openxr-triangle` | Tracked eye views, linear eye output, desktop stereo mirror |
| OpenXR Sponza | `example-openxr-sponza` | Original scene assets, separate renderer/graph and parameter buffers per eye, tracked pose plus movable camera rig |
| RenderGraph editor | — | Deferred; graph construction remains code-driven, as requested |
| Gaussian Splatting | — | Excluded from this migration, as requested |

Sponza uses the current OpenPBR forward renderer, with indexed and task/mesh geometry paths. The meshlet example inherits `dev`'s meshoptimizer v0.24 construction per submesh, 64-vertex / 124-triangle limits, cone weight 0.5 and 32-thread task groups. Current scene primitives replace the old submesh container; global indices are rebased for construction. The GPU format packs each local triangle into a 32-bit word for Slang reads. Material boundaries and authored vertex attributes remain intact.

Both paths share OpenPBR, normal/emission textures, IBL and CSM/PCF/PCSS. Shadow cascades still use indexed draws. Task shaders currently cull bounding spheres against the camera frustum; cone culling is omitted because the current renderer draws both sides of all materials. The old deferred path, point lights and LTC area lights are not part of this port. Its asset set matches `dev`; the full old lighting setup does not. Cornell Box retains the old example's centroid-light approximation and is not a path tracer. Ray examples handle static opaque, untextured geometry.

The two OpenXR examples share a small example-local lifecycle in `examples/common/xr_sample.*`. It owns acquisition, GPU completion, release, mirror rendering and desktop GUI. Rendering each eye remains in the individual example. The Sponza renderer tone maps to linear RGBA16F before drawing into the runtime's selected eye format, avoiding a second sRGB encode.

Desktop finite-frame captures can verify these paths. OpenXR compilation and offscreen color tests do not establish headset validation; a compatible active headset is still required.

Original source assets remain unchanged. Derived caches are separate; see [Asset Pipeline](asset_pipeline.md) and [asset attribution](../resources/README.md).
