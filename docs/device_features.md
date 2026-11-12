# Device features

`Device::features()` returns a `DeviceFeatures` struct filled from what VRI reports once the device exists. A
render path, a stage or a native pass checks it before it relies on a capability, and fails with an error that
names the missing feature; there are no silent fallbacks.

This page records, for each capability that advanced rendering techniques depend on, whether VRI exposes it,
on which backends, and how that was established. It is a dated audit (2026-11-12, VRI at the version
libvultra pins, read from the local `fix/vulkan-surface-transform` checkout); check it against VRI before
relying on it.

## Requested and reported

Ray tracing pipelines, ray queries, mesh shading, variable-rate shading and bindless descriptors are optional
VRI features (`VriFeatureBits`). On Vulkan a device has them only when `Device::create()` requested them and
the driver granted them; `DeviceFeatures` then reports what was granted. Metal reports ray queries and mesh
shading from the GPU family without a request. A program that wants such a feature has to request it when it
creates the device: the `vultra` command line requests none today.

## Audit

Evidence: **run here** means exercised by a test or example on this machine (Windows 11, RTX 4090 Laptop,
Vulkan); **reported here** means the device reported it granted here, but nothing used it; **source** means read
from VRI's source only; **not exposed** means VRI has no way to request or use it.

| Capability | `DeviceFeatures` | Vulkan | Metal | WebGPU | Evidence |
| --- | --- | --- | --- | --- | --- |
| Independent compute queue | `asyncCompute` | A second queue of the graphics family when the family has one. | A second `MTLCommandQueue`; whether it overlaps graphics is up to the GPU. | No: one queue. | Vulkan: run here (`test-graph-async`). Metal, WebGPU: source. |
| Ray query and acceleration structures | `rayQuery`, `rayTracingPipeline` | `VK_KHR_ray_query`, `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline` when requested. | Ray queries when the GPU supports ray tracing; no ray tracing pipeline. | No. | Vulkan: reported here. Metal, WebGPU: source. |
| Mesh shading | `meshShading` | `VK_EXT_mesh_shader` (task and mesh) when requested. | Metal 3 GPU family. | No. | Vulkan: run here (`test-meshlets`, `example-basics mesh-shading`). Metal, WebGPU: source. |
| 64-bit atomics | none | Not exposed in the released VRI: no feature bit, and the backend does not enable `shaderInt64` or 64-bit atomic features. | Not exposed. Metal Shading Language has 64-bit `ulong` atomics for min and max only, on newer GPU families; VRI does not wire them. | Not exposed; WebGPU has no 64-bit atomics. | Source; see the decision below for the local VRI branch. |
| Sparse textures | none | Not exposed: no sparse or tiled resource API. | Not exposed. | Not exposed. | Source. |
| Indirect draw with a count | `drawIndirectCount` | Vulkan 1.2 `drawIndirectCount`, enabled when supported. | No: the command is an empty stub and the flag is false. | No. | Vulkan: reported here, not run. Metal, WebGPU: source. |
| Variable-rate shading | `variableRateShading` | `VK_KHR_fragment_shading_rate` when requested. | No. | No. | Vulkan: reported here, not run. Metal, WebGPU: source. |
| Multiview | `multiview`, `maxViews` | Vulkan 1.1 multiview, enabled when supported. | Every Apple GPU, through instanced layered rendering. | No. | Vulkan: reported here (32 views); no libvultra code renders with a view mask yet. Metal, WebGPU: source. |
| Bindless descriptors | `bindless` | Descriptor indexing when requested. | Tier 2 argument buffers when requested. | No. | Vulkan: reported here. Metal, WebGPU: source. |
| Wave (subgroup) operations | `waveOperations` | Subgroup operations when supported. | Not reported (false). | No. | Vulkan: reported here, not run by a libvultra test. Metal, WebGPU: source. |
| Anisotropic filtering | none | Enabled when supported; a sampler's request is clamped to the device limit. | Honoured by the sampler. | Honoured by the sampler. | Source. VRI does not report support or the limit, so `DeviceFeatures` cannot either. |
| Block-compressed textures | `blockCompression` | BC1 to BC7, through `GetFormatSupport`. | Through `GetFormatSupport`. | Through `GetFormatSupport`. | Vulkan: run here (a BC7 texture and view in `test-device`). Metal, WebGPU: source. ASTC and ETC2 are not VRI formats. |

The WebGL2 backend and VRI's D3D12 and OpenGL backends were not audited: libvultra does not build them for
these techniques.

## Gaps and decisions

64-bit atomics and sparse textures are missing from VRI on every backend. Variable-rate shading, indirect draws
with a count and wave operations are exposed on Vulkan only. The owner decided on 2026-11-12:

- **64-bit atomics** go into VRI: `VriFeature_ShaderAtomicInt64` with `VriDeviceDesc::hasShaderAtomicInt64`.
  Vulkan grants it from `shaderInt64` and `shaderBufferInt64Atomics`, D3D12 from Shader Model 6.6 and
  `Int64ShaderOps`; both were run here with a 64-bit `InterlockedMax` and `InterlockedAdd`. Metal refuses the
  request: its shading language has 64-bit min and max only, on newer GPU families, and wiring that through
  VRI's SPIR-V to MSL translation was not attempted without a Mac to test on. This covers storage buffers, not
  64-bit image atomics. The change is on the local VRI branch `feat/int64-atomics` and is not pushed.
  libvultra builds VRI from a released archive, so `DeviceFeatures` gains the flag only once a VRI release
  carries it.
- **Sparse textures** are not added to VRI. A technique that needs virtual textures or virtual shadow maps pages
  them in software: physical pages live in an ordinary texture atlas, and a page table (a texture or buffer the
  technique owns) maps virtual pages to atlas tiles, read before every sample. This works on every backend and
  needs nothing from the device; the cost is the indirection and the page-table upkeep, which the technique
  measures.
