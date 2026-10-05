# Example Coverage Against `dev`

The reference is libvultra's old `dev` branch, not `dev-next`. This table separates example coverage from renderer feature parity. The original demonstrations are grouped into category targets with selectable modes. Category targets are enabled by default. Unsupported device features produce an explicit creation error; the examples do not substitute software rendering.

| `dev` example | `dev-VRI` target | Current implementation |
| --- | --- | --- |
| Window | `example-basics window` | Same authored clear color through an sRGB swapchain |
| RHI triangle | `example-basics vri` | Indexed VRI draw, black background, sRGB swapchain |
| FrameGraph triangle | `example-basics graph` | Vultra's own code-driven graph; no external fg dependency |
| ImGui | `example-ui` | Raw ImGui, EditorGui and RmlUi VGui edit one state; docking, native viewports, render-target preview, theme selection and PNG capture remain available |
| Debug Draw | `example-scene debug` | Damaged Helmet with the original HDR, actual world-space model bounds, grid, axes and sphere wireframes |
| glTF Viewer | `example-scene helmet` | Damaged Helmet, model selection, OpenPBR subset, IBL and CSM/PCF/PCSS |
| Sponza | `example-scene sponza` | Original glTF, buffers, textures and HDR; first-person camera; built-in forward passes |
| Sponza meshlet path | `example-scene sponza-mesh-shading` | `dev` meshoptimizer clusterization, task-stage frustum culling, mesh drawing, meshlet colors and an indexed comparison switch |
| Ray Query | `example-ray query` | Original Armadillo/floor scene; rasterization plus fragment-stage hardware shadow queries |
| Ray Tracing Triangle | `example-ray triangle` | Hardware raygen, miss, closest-hit and shader binding table |
| Ray Tracing Cornell Box | `example-ray cornell` | Original OBJ/MTL; primary rays and secondary shadow rays to the emissive quad centroid |
| Mesh Shading Triangle | `example-basics mesh-shading` | Slang task/amplification, mesh and fragment stages; one shared reference triangle |
| OpenXR Triangle | `example-xr triangle` | Tracked eye views, linear eye output, desktop stereo mirror |
| OpenXR Sponza | `example-xr sponza` | Original scene assets, separate renderer/graph and parameter buffers per eye, tracked pose plus movable camera rig |
| RenderGraph editor | — | A read-only compiled-graph observer is available in `example-research`; graph construction remains code-driven |
| Gaussian Splatting | — | Excluded from this migration, as requested |

Triangle examples use the same counterclockwise vertices: top `(0, 0.5, 0)` red, bottom-left `(-0.5, -0.5, 0)` green and bottom-right `(0.5, -0.5, 0)` blue. Colors interpolate in linear RGB and receive exactly one sRGB encode for display. RHI/RenderGraph/mesh shaders use sRGB attachments; ray tracing encodes for its UNORM desktop target. XR writes linear eye color and the mirror encodes it for display. Its tracked stereo projection changes screen placement, not the triangle's geometry or colors. C++ examples share `kTriangleVertices`; procedural shaders share `examples/common/triangle.slangh`. The display regression compares these definitions and all four XR eye formats against the indexed reference.

Sponza uses the current OpenPBR forward renderer, with indexed and task/mesh geometry paths. The meshlet example inherits `dev`'s meshoptimizer v0.24 construction per submesh, 64-vertex / 124-triangle limits, cone weight 0.5 and 32-thread task groups. Current scene primitives replace the old submesh container; global indices are rebased for construction. The GPU format packs each local triangle into a 32-bit word for Slang reads. Material boundaries and authored vertex attributes remain intact.

Both paths share OpenPBR, normal/emission textures, IBL and CSM/PCF/PCSS. Shadow cascades still use indexed draws. Task shaders currently cull bounding spheres against the camera frustum; cone culling is not implemented. Rasterization respects each material's sidedness. Research has a small indexed `NaiveDeferred` path with two G-buffer draws; the current SceneTree adds directional/point/spot lights, while the old branch's Lua graph format and LTC area lights are not ported. Its asset set matches `dev`; the full old lighting setup does not. Cornell Box retains the old example's centroid-light approximation and is not a path tracer. Ray examples handle static opaque, untextured geometry. A separate [reference path](reference_renderer.md) in batch/workbench/Python supports the current opaque textured material subset and progressive AOVs.

The two OpenXR examples share a small example-local lifecycle in `examples/common/xr_sample.*`. It owns acquisition, GPU completion, release, mirror rendering and desktop GUI. Rendering each eye remains in the individual example. The Sponza renderer tone maps to linear RGBA16F before drawing into the runtime's selected eye format, avoiding a second sRGB encode.

Desktop finite-frame captures can verify these paths. OpenXR compilation and offscreen color tests do not establish headset validation; a compatible active headset is still required.

Original source assets remain unchanged. Derived caches are separate; see [Asset Pipeline](asset_pipeline.md) and [asset attribution](../resources/README.md).
