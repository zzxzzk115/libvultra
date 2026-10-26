# VRI dependency

The `vri-vultra` package inherits the shared xmake `vri` recipe and is exposed to targets as `vri`.
It builds VRI 0.2.0 from merged commit
[`d0c78964cc50788eb49d4b7daf967ef3222d2530`](https://github.com/zzxzzk115/VRI/tree/d0c78964cc50788eb49d4b7daf967ef3222d2530),
not a release tag. The archive SHA-256 is
`b236bd95ad4a9f4f04ffbbf61cbff02934cdcfeac740963e8dd8f2c1025a62ad`.
VRI is MIT licensed; its [license](LICENSE) is retained. Backend selection and static linkage remain explicit.

The merged source includes persistent partial WebGPU descriptor writes and GPU-complete fence/idle semantics.
Their previous local patches have been removed. These remaining patches are checksum-pinned against that commit:

| Patch | Scope and reason |
| --- | --- |
| `calibrated_timestamps.patch` | Vulkan: select KHR/EXT timestamp-domain entry points from the enabled extension on that adapter; mixed Intel/NVIDIA ICDs must not use another ICD's trampoline. |
| `validation_extent.patch` | Forward actual swapchain extent through validation; resize must not use a larger requested render area. |
| `cube_array.patch` | Enable the queried Vulkan `imageCubeArray` device feature used by texture-array sampling. |
| `texture_copy_region.patch` | Vulkan: honor texture-copy offsets, extents, mip levels and array layers instead of copying the entire base image. The graph partial-write regression checks preserved and replaced texels. |
| `gl_sampler_mips.patch` | WASM: honor mip filtering and min/max sampler LOD. |
| `web_presentation.patch` | WASM: WebGPU canvas copy/sRGB-view use and explicit WebGL sRGB presentation. |
| `webgpu_compute_transfer.patch` | WASM: end an implicit compute pass before buffer-to-texture copies. |
| `webgpu_row_alignment.patch` | WASM: report WebGPU's 256-byte copy-row alignment for upload/readback layouts. |
| `android_archs.patch` | Android emulator: admit x86_64 as well as ARM64; both ABIs remain 64-bit. |

Texture, resize, odd-sized transfer and platform readback tests exercise these behaviors; they are not standards
conformance or headset acceptance. Remove each patch when a selected upstream commit includes its behavior.

`Device` accepts a VRI creation descriptor directly. Its convenience constructors explicitly select Y-up for the
existing builtin shaders/cameras; VRI's default descriptor still selects Vulkan conventions. Frame submission,
completion and explicit idle waits check their results. Destruction reports idle failure without throwing.

When rebuilding dependencies, preserve the configured version/backend flags: an unqualified package requirement
can select a different package. A changed static package requires relinking its consumers. Browser adapter/device
startup, native Linux WebGPU surfaces and hardware acceptance remain independent platform gates.

The checksum-pinned `calibrated_timestamps.patch` selects the KHR or EXT physical-device timestamp
entry point from the extension actually enabled on that GPU. Instance-level KHR lookup alone can
return another ICD's trampoline on hybrid-GPU systems. It applies to the pinned v0.2.0 source;
retain the current dependency revision and other platform patches when rebuilding it.
