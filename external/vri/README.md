# VRI dependency

The `vri-vultra` package inherits the shared xmake `vri` recipe and is exposed to targets as `vri`.
It builds VRI 0.2.0 from merged commit
[`d0c78964cc50788eb49d4b7daf967ef3222d2530`](https://github.com/zzxzzk115/VRI/tree/d0c78964cc50788eb49d4b7daf967ef3222d2530),
not a release tag. The archive SHA-256 is
`b236bd95ad4a9f4f04ffbbf61cbff02934cdcfeac740963e8dd8f2c1025a62ad`.
Original VRI code is MIT licensed; its [license](LICENSE) is retained. The Metal texture-clear patch additionally
embeds Slang-RHI shader code under Apache-2.0 WITH LLVM-exception, preserving its license in the patched source.
Backend selection and static linkage remain explicit.

The merged source includes persistent partial WebGPU descriptor writes and GPU-complete fence/idle semantics.
Their previous local patches have been removed. These remaining patches are checksum-pinned against that commit:

| Patch | Scope and reason |
| --- | --- |
| `calibrated_timestamps.patch` | Select the timestamp-domain entry point for the adapter's enabled Vulkan extension. |
| `vulkan_xr_cache_control.patch` | Enable queried Vulkan pipeline cache control on XR-hooked devices. |
| `moltenvk_adapter_enumeration.patch` | Enumerate the same portability adapters that native device creation can select on macOS. |
| `acceleration_scratch_rebuild.patch` | Reacquire released Vulkan AS scratch for an ordinary in-place rebuild, retain AS addresses and restore object footprint accounting. Allocation failure becomes a recording result returned by EndCommandBuffer. Compacted destinations reject rebuilding. |
| `metal_acceleration_scratch.patch` | Implement completed-use scratch release and reacquisition for Metal AS builds; track backing, scratch and translated instance allocations. Report scratch/instance allocation and encoder creation failure through existing recording results. |
| `vulkan_recording_result.patch` | Retain/reset recording failures on non-macOS Vulkan, where the existing MoltenVK plane-copy patch does not supply that state. Applied before the scratch-rebuild patch; no public ABI expansion. |
| `storage_buffer_arrays.patch` | Report enabled non-uniform storage-buffer indexing and ordinary descriptor limits on Vulkan/Metal. Enable the supported Vulkan feature with bindless, and expose an array-layout flag that retains Metal argument-buffer arrays even when their capacity is one. |
| `metal_buffer_dimensions.patch` | Supply SPIRV-Cross buffer-view byte lengths for structured/raw `GetDimensions`, both direct and argument-buffer bindings. Honor offset/size and whole-remaining views, retain metadata across encoder/pipeline changes, reserve a free per-stage native slot and reject exhausted slots or unrepresentable queried lengths. Preserve vertex streams when metadata temporarily uses their slot. A requested fragment/task stage must succeed. No public VRI ABI change. |
| `descriptor_binding_flags.patch` | Enable queried Vulkan update-after-bind features by descriptor type, and use those actual enabled types when choosing binding/layout/pool flags. Preserve partially-bound and variable-count binding flags independently of update-after-bind, reject sparse/variable layouts without enabled bindless features, and report ordinary texture/sampler limits when that type cannot use update-after-bind. No public VRI ABI change. |
| `vulkan_color_input.patch` | Expose a Vulkan two-phase framebuffer-local color input pass. The raster phase clears the input and optional depth. The resolve phase reads input attachment index 0, writes color and preserves depth. The patch includes typed descriptor support and recording-scope validation. Input contents are discarded. Callers own external layout transitions and attachment lifetimes. The optional interface is Vulkan-only. |
| `vulkan_graphics_bindings.patch` | Map vertex attributes through the declared stream's binding slot, enable queried fragment storage/atomic support, and normalize validated size-zero buffer views to explicit remaining byte ranges. The numerical length-query fixture exposed a nonzero-slot validation failure and a writable fragment storage declaration rejected despite device support. |
| `metal_argument_padding.patch` | Preserve descriptor slots removed from individual shader entry points by Slang. Supply member types/counts to SPIRV-Cross padding while keeping argument-container and push-constant mappings out of the member-index lookup. |
| `metal_compute_state.patch` | Restore native compute pipeline, descriptors, argument-buffer residency and inline constants when a timestamp or transfer ends the compute encoder. Reset cached bindings when recording a new command buffer. |
| `metal_command_allocator.patch` | Make allocators own their Metal command wrappers and scratch/visibility buffers; reset or destroy abandons unsubmitted encoders and releases native command buffers after caller-established completion. |
| `metal_command_autorelease.patch` | Drain the command buffer's temporary autorelease at acquisition so the allocator's explicit release can free an unsubmitted buffer. |
| `metal_texture_clear.patch` | Implement typed storage-texture clears with device-owned Metal kernels, preserve caller compute state, and report compressed/integer/depth formats accurately. Reject unsupported texture usages before native allocation. |
| `texture_clear_ranges.patch` | Add explicit mip/layer color-storage clear ranges, result/capability reporting, Vulkan recording and validation forwarding. |
| `metal_texture_clear_ranges.patch` | Use the existing typed Metal kernels for selected native mip/layer ranges and full 3D depth while preserving compute bindings. |
| `explicit_memory_contract.patch` | Report required dedicated allocations and Vulkan buffer/image granularity; validate Vulkan placed bindings and map borrowed buffer allocations at their actual offsets. Reject invalid explicit allocation descriptors on Vulkan and D3D12. |
| `metal_memory_offsets.patch` | Use Metal placement heaps, honor native buffer/texture offsets, validate ownership/bounds/single binding, and preserve unbound resources after rejection. Validate mapped buffer ranges and host visibility. |
| `alias_memory_barrier.patch` | Append a recording-only memory-dependency command/capability, validate command scope, and implement Vulkan global memory barriers through the existing core/KHR entry-point resolver. |
| `metal_alias_memory_barrier.patch` | Order distinct alias handles with a command-owned GPU event between encoders; use tracked placement heaps to preserve ordinary resource hazard ordering. |
| `buffer_range_barriers.patch` | Append buffer-local offset/size to buffer barriers and a result/capability entry point; validate command scope through the validation layer and native buffer ownership/bounds. Vulkan records byte ranges through the existing core/KHR barrier resolver. |
| `metal_buffer_range_barriers.patch` | Expose checked byte-range recording while retaining Metal's conservative automatic resource hazards; no promise of physical byte-level overlap. |
| `depth_stencil_clear_ranges.patch` | Append result/capability reporting for complete selected depth/stencil mip/layer/aspect clears, including native MSAA. Validate scope, values, ownership, binding and usage; Vulkan records native image clears on its graphics-capable pools. |
| `metal_depth_stencil_clear_ranges.patch` | Use private render-pass clears for selected mip/layer/aspect cells, preserve unselected planes and user binding caches, and report encoder failure. Track declared usage for created/wrapped textures. |
| `metal_texture_views.patch` | Honor stencil-plane views through X32/X24 stencil formats, preserve multisample array texture/view types, validate ranges/type/aspects before Metal calls, and bound native view creation with an autorelease pool. |
| `mutable_texture_formats.patch` | Add explicit MutableFormat creation and a native format/usage compatibility query. Vulkan creation and memory queries share a compatible format list; native views enforce actual resource ownership and permission. Unsupported backends reject mutable creation. |
| `metal_mutable_texture_formats.patch` | Implement checked Metal color-format views, reserve PixelFormatView for mutable/stencil resources, and enable queried Vulkan portability format reinterpretation on macOS. |
| `metal_texture_dimensions.patch` | Reject native Metal 1D/1D-array mip chains with Unsupported before texture allocation, avoiding a Metal API assertion. |
| `vulkan_portability_features.patch` | On macOS Vulkan, query and enable the supported portability-subset multisample-array feature. Reject unsupported MSAA-array texture creation through VriResult instead of issuing an invalid Vulkan call. |
| `vulkan_compute_queue_family.patch` | Opt in to a separate Vulkan compute family when a second graphics-family queue is unavailable. Publish the command buffer's native pool family through `GetCommandBufferQueueFamilyIndex` and reject submissions to a different family. Extend the creation descriptor and core interface. Rebuild consumers against the patched ABI. |
| `command_allocator_validation.patch` | Make command-validation wrappers allocator-owned, reset all of that allocator's recording scopes, preserve unrelated allocators and commit begin state only on success. |
| `astc_formats.patch` | Expose optional ASTC 4x4/6x6 linear and sRGB formats on Vulkan and Metal, and calculate Metal BC/ASTC copy pitches in blocks; applications query actual device support. |
| `bc_srgb.patch` | Expose native BC1/2/3/7 sRGB sampling in Vulkan, Metal and D3D12; existing format values stay unchanged. |
| `depth_attachment_resolve.patch` | Append the depth resolve capabilities to `VriDeviceDesc` and use the existing depth attachment `resolveView` on Vulkan. The capability requires native sample-zero resolve. Vulkan retains the depth layout and uses `VK_RESOLVE_MODE_SAMPLE_ZERO_BIT`. |
| `metal_depth_attachment_resolve.patch` | Enable D16/D32 sample-zero depth resolve on Apple3 or Mac2 families and honor both resolve-only and store-and-resolve actions. |
| `stencil_attachment_resolve.patch` | Publish native stencil resolve capabilities and select sample zero on Vulkan. An S8 pipeline declares its stencil format without a depth format. |
| `metal_stencil_attachment_resolve.patch` | Use the native sample-zero stencil filter and both source store actions on Apple5 or Mac2 families. |
| `readonly_depth.patch` | Add an explicit preserving read-only depth attachment: Vulkan uses the read-only layout and `STORE_OP_NONE`, D3D12 binds a read-only DSV, WebGPU uses read-only depth/stencil flags, and Metal/OpenGL preserve loaded contents with pipeline writes disabled. Also honor Vulkan depth `DontCare` stores. |
| `vulkan_depth_load.patch` | Honor depth/stencil `DontCare` loads on Vulkan. The previous mapping treated every non-clear operation as `LOAD`, introducing an undeclared depth read in passes that discard and replace depth. |
| `vulkan_stencil_attachments.patch` | Vulkan: honor explicit stencil operations and stencil-only binding, retaining the combined-depth convenience; preserve unused depth against MoltenVK attachment substitution, with an internal memory dependency. |
| `vulkan_storage_features.patch` | Enable queried extended-format and formatless storage-image shader features on Vulkan, including Vulkan 1.2 devices. |
| `vulkan_float16_features.patch` | Independently enable queried Vulkan FP16 arithmetic and 16-bit buffer/uniform/push/input-output storage bits. Native half-uniform shaders previously produced validation errors despite physical-device support. The package config distinguishes the rebuilt static library; no public VRI ABI change or emulation. |
| `texture_aspect_states.patch` | Append an enabled independent depth/stencil-layout capability to VriDeviceDesc and enable the queried Vulkan feature. Public ABI change: rebuild all consumers. |
| `metal_texture_plane_transfers.patch` | Select depth/stencil buffer-blit options and plane byte sizes for combined Metal formats. Applied only after macOS native patches. |
| `metal_texture_plane_copy.patch` | Preserve the unselected plane in Metal combined depth/stencil image copies using allocator-owned reusable GPU staging buffers; reject unsupported multisample copies before submission. |
| `moltenvk_texture_plane_copy.patch` | Work around MoltenVK same-format copies overwriting both planes. Use explicitly synchronized reusable buffer transfers only for a selected combined-format plane on the MoltenVK driver. |
| `linear_filter_support.patch` | Add `VriFormatSupport_LinearFilter`: Vulkan queries sampled-image plus linear-filter features, Metal checks integer/float32 format restrictions and the device property, D3D12 uses shader-sample support, and WebGPU reports only filtering available without optional float32 features. |
| `validation_extent.patch` | Forward actual swapchain extent through validation; resize must not use a larger requested render area. |
| `vulkan_swapchain_format.patch` | Vulkan: reject a requested surface format that is unavailable instead of silently creating an incompatible image. The host can then try the other channel order with the same color encoding. |
| `vulkan_composite_alpha.patch` | Vulkan: select an advertised surface composite-alpha mode, preferring opaque, and fail if capabilities cannot be queried or no mode is supported. Some Android surfaces expose only inherit. |
| `cube_array.patch` | Enable the queried Vulkan `imageCubeArray` device feature used by texture-array sampling. |
| `texture_copy_region.patch` | Vulkan: honor texture-copy offsets, extents, mip levels and array layers instead of copying the entire base image. The graph partial-write regression checks preserved and replaced texels. |
| `gl_sampler_mips.patch` | WASM: honor mip filtering and min/max sampler LOD. |
| `web_presentation.patch` | WASM: WebGPU canvas copy/sRGB-view use and explicit WebGL sRGB presentation. |
| `webgpu_compute_transfer.patch` | WASM: end an implicit compute pass before buffer-to-texture copies. |
| `webgpu_row_alignment.patch` | WASM: report WebGPU's 256-byte copy-row alignment for upload/readback layouts. |
| `android_archs.patch` | Android emulator: admit x86_64 as well as ARM64; both ABIs remain 64-bit. |
| `android_depth_array_clear.patch` | Android Qualcomm: preserve earlier depth-array layers when multiple clears are recorded in one command buffer, using an attachment clear within the same rendering scope. |
| `android_array_resolve_dependency.patch` | Android Qualcomm: record a GPU memory dependency after MSAA array color resolves so later render scopes/readback observe updated selected layers. Reuse the existing Vulkan dependency operation; add no CPU wait, submission, resource allocation or public ABI. |
| `android_draw_parameters.patch` | Android: report the required `shaderDrawParameters` capability as unsupported before device creation, instead of a generic Vulkan failure. It does not make unsupported devices render. |
| `macos_bindless_limits.patch` | macOS Vulkan: report the ordinary descriptor limits used by MoltenVK's fixed-size fallback instead of the larger update-after-bind limits. The package config distinguishes this patched static build from older cached builds. |
| `metal_openxr_interop.patch` | Metal: create on the OpenXR-selected device and expose native device, queue and borrowed swapchain texture interoperability. |
| `metal_ray_wave_features.patch` | Metal: grant requested inline ray query only when supported, and report Metal 3 wave operations. A package config forces consumers to link the patched static library. |
| `metal_ray_instance_id.patch` | Metal: preserve the 24-bit custom instance ID in ray queries using user-ID instance descriptors. The instance layout is selected before acceleration-structure size queries. |

`VriTextureUsage_MutableFormat` is explicit creation permission, not an instruction to convert storage. The appended
`GetTextureViewFormatSupport` entry validates source/view compatibility and all declared usages; callers still
check actual creation/view results and dimension limits. Compatible color formats follow native texel-bit classes;
compressed views retain their codec/block shape and may select its linear/sRGB variant. Vulkan also enforces the
enabled portability-subset component-layout feature. Depth/stencil color casts are excluded. Native view creation
checks the real resource's owner and permission, including resources reached through a borrowed wrapper.

Vulkan uses the same `VkImageFormatListCreateInfo` and mutable flag in creation and explicit-memory queries,
including exportable creation. The external-memory path has not received runtime qualification for this patch.
Metal's ordinary same-format range/type views omit PixelFormatView; mutable/stencil resources retain it. Apple
[documents a possible compression cost](https://developer.apple.com/videos/play/wwdc2021/10148/?time=924) for that
flag; no frame-time improvement has been measured here. Compatibility follows the documented
[Metal view groups](https://developer.apple.com/documentation/metal/mtltexture/maketextureview(pixelformat:)) and
[Vulkan image-view rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html).
D3D12, WebGPU and OpenGL reject MutableFormat rather than emulate it; those paths remain runtime-unqualified.
The `texture_view_formats` package fingerprint distinguishes this ABI. Rebuild and relink all consumers; the
upstream version/revision stays pinned.

`CmdAliasMemoryBarrier` orders prior and later GPU memory accesses, including distinct handles bound to the same
allocation. It requires recording outside rendering and reports unsupported backends through capability/result.
It does not initialize image contents/layouts, transfer queue ownership or supply cross-queue waits. Vulkan uses
the resolved core/KHR synchronization2 command, including the MoltenVK 1.2 baseline. Metal ends auxiliary encoders,
then signals/waits on a private `MTLEvent` with values monotonic across command-buffer resets. There is no CPU wait.
Placement heaps explicitly enable hazard tracking; the native default is untracked. The event survives discarded
recordings and is released with its command allocator after caller-established completion. The interface/device
layout expansion requires rebuilding and relinking consumers; `alias_memory_barrier` distinguishes the package.
The macOS recording-result check belongs to the plane-copy patch and is platform-guarded. Current graph probes
exercise mixed texture/buffer activation, different-description R32_UINT/RGBA8/RGBA16F/RGBA32F and D32 image pairs,
array/cube/3D mip/layer/depth coverage, depth views, routed/forked access and failed-recording recovery on macOS Metal,
MoltenVK and Android Adreno. Other backends and compressed/stencil/MSAA image aliases remain unqualified.

`VriAttachmentDesc::readOnly` is valid only on the depth slot, including the stencil aspect of a combined view.
It requires `Load` and `Store`, no resolve target, no separate stencil attachment, and disabled depth/stencil pipeline
writes. VRI validation rejects incompatible attachment parameters; callers remain responsible for pipeline write
state. `Store` expresses preservation here. On Vulkan the backend implements it with
[`VK_ATTACHMENT_STORE_OP_NONE`](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentStoreOp.html),
which is available with the backend's required dynamic-rendering support. Metal has no equivalent no-store action,
so it preserves the attachment with `MTLStoreActionStore`. OpenGL uses the pipeline's depth/stencil write masks.
The new package configuration changes the static package identity; rebuild and relink all consumers after this ABI
change. Windows D3D12 and browser WebGPU/OpenGL runtime checks remain outstanding.

For a writable depth attachment, `DontCare` now maps to `VK_ATTACHMENT_LOAD_OP_DONT_CARE`, including the stencil
aspect of a combined view. Per the [Vulkan load-operation contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentLoadOp.html),
`LOAD` reads existing attachment contents, whereas `DONT_CARE` uses attachment-write access. A depth producer declared
as a graph write must not acquire an implicit load because of backend translation. The preserving read-only
contract still requires `Load`; this patch does not discard those contents. Relink Vulkan consumers to use the fix.

`VriFormatSupport_Texture` does not imply linear filtering. The separate `LinearFilter` bit describes enabled native
sampling support: Vulkan requires both sampled-image and
[linear-filter format features](https://docs.vulkan.org/refpages/latest/refpages/source/VkFormatFeatureFlagBits.html),
and Metal consults the device's
[`supports32BitFloatFiltering`](https://developer.apple.com/documentation/metal/mtldevice/supports32bitfloatfiltering)
property for float32 color formats. Integer formats cannot be consumed by a floating-point filtered shader.
WebGPU does not request its optional float32-filtering feature; OpenGL currently leaves the new bit unreported.
The D3D12, WebGPU and OpenGL paths have not received runtime acceptance for this patch. Rebuild and relink all
consumers so the format query and callers agree; do not infer support from a successful texture allocation.

The storage-array patch appends `storageBufferDescriptorMaxNum` and `hasStorageBufferNonUniformIndexing` to
`VriDeviceDesc`. The flag reports an enabled capability; sampled-image bindless alone does not imply it. The count
bounds ordinary storage descriptors visible to one stage across all sets. Vulkan currently reports its maximum
storage-buffer descriptor range through `bufferMaxSize`. A large underlying allocation can therefore need several
views. `VriDescriptorRange_Array` preserves resource-array addressing on Metal, including capacity one, without
requesting partially-bound or update-after-bind descriptors on Vulkan. Rebuild and relink consumers for this ABI.

Metal argument-buffer layouts must also preserve resources removed from the SPIR-V for a particular entry point.
The padding patch registers the complete member layout before compilation. Container-buffer and push-constant
remappings are separate from the member-index lookup. The visibility compute fixture reproduced a missing middle
slot as an all-sky result before the fix; its hole mask then read correctly with padding enabled.

Metal timestamp sampling and transfers can close a compute encoder between binding and dispatch. The command
buffer now remembers the native buffer, texture and sampler tables, including inline bytes and argument-buffer
residency, and restores them when creating the next compute encoder. Buffer views, acceleration structures,
argument buffers and inline constants share the native buffer-slot namespace: a later binding replaces the previous
entry. Inline bytes are copied; descriptor and argument-set references remain borrowed and must outlive their
recorded GPU uses. New recording clears those references while retaining cache capacity. The `metal_compute_state`
package config selects a new static package identity; rebuild and relink Metal consumers.

`test-compute-encoder-state` reproduces the previous nil-pipeline assertion with Metal API Validation and checks
six exact GPU outputs through timestamps, transfers, storage-texture clears when supported, partial
descriptor/constant edits, pipeline changes, indirect dispatch and three Frame reuse cycles. Its raw timestamp
output is diagnostic; successful data checks do not certify
timestamp ordering. MoltenVK 1.2.11 can return incomplete GPU query copies even with `WAIT_BIT`. A separate direct
Vulkan reproduction returns a zero final value from an in-submission copy while host query results after fence
completion contain all five timestamps; a separate copy submission matches those host results. This SDK issue is
unfixed and is not covered by the Metal state patch.

The unpatched pinned Metal implementation has no storage-texture clear provider. `metal_texture_clear.patch`
implements the existing command for mip 0, all array/cube layers and full 3D depth, leaving other mips unchanged.
Device initialization creates typed float/uint/int pipelines for seven native texture shapes; failure rejects device
creation before advertising the capability. A private compute encoder preserves storage-only texture usage and
the caller's cached pipeline, descriptors and inline constants. Its autorelease scope also bounds the encoder's
temporary lifetime for C++ callers without a Cocoa run loop. Recording remains outside a render pass, with the
existing transfer-domain barriers and caller-established completion requirements.

The kernels are adapted from [Slang-RHI's implementation](https://github.com/shader-slang/slang-rhi/blob/30250fa108081ef1c2949c332ec41d104ffa1c95/src/metal/shaders/clear-texture.metal).
The patch retains its Apache-2.0 WITH LLVM-exception notice and `LICENSE-slang-clear.txt`; the package's macOS
license metadata includes this component. The original mip-zero patch preserves the existing command ABI.
`metal_texture_clear` distinguishes the patched Metal static package; rebuild and relink consumers.

`explicit_memory_contract.patch` expands `VriMemoryDesc` with `requiresDedicatedAllocation` and
`VriDeviceDesc` with `bufferTextureGranularity`. Rebuild consumers for these native layout changes;
`explicit_memory_contract` distinguishes the static package, without changing the pinned upstream version.
Vulkan uses core-1.1 requirements queries (including dedicated flags), preserving the MoltenVK 1.2 baseline.
Bindings check the actual native resource's requirements and selected VMA memory type. The borrowed allocation
and explicit buffer offset are retained for mapping; destroying the buffer does not free that allocation.
`metal_memory_offsets.patch` replaces automatic heaps with
[placement heaps](https://developer.apple.com/documentation/metal/mtlheaptype/placement) and uses the offset-aware
buffer/texture creation methods. Rejected bindings preserve the unbound native object/descriptor for retry.

`test-explicit-memory` covers three host-visible locations, same-offset and disjoint buffers, invalid allocation/
binding/mapping and cross-device ownership, explicit queue-fence alias transfers, plus 12 device-heap texture
format/shape groups with every mip/layer/full 3D depth and an adjacent buffer canary. Metal, MoltenVK and Android
Adreno pass exact data readback. This is a native allocation prerequisite, not general graph heap aliasing or image-
alias activation; callers retain heaps/resources and establish GPU memory dependencies through final use.
D3D12 descriptor checks are changed but unqualified, and other native backends remain outside this qualification.

`texture_clear_ranges.patch` appends `CmdClearStorageTextureRange` to the core interface and
`hasClearStorageTextureRange` to the device description. Rebuild all consumers for these expanded native layouts;
the `storage_texture_ranges` package configuration distinguishes them from prior static packages. The pinned
revision/dependency versions remain unchanged. `VriTextureSubresourceRange` selects complete mip/layer ranges;
zero counts select the remaining levels/layers and a null range selects the whole texture. A 3D mip includes its
full depth. This is a color-storage clear, outside rendering, with storage/transfer-destination usage and caller
CopyDestination barriers. It does not clear rectangles, depth/stencil planes or compressed textures.

The command returns a result; invalid range/availability rejects before clear recording, and a recording failure
requires discarding the command buffer. Vulkan records the selected `vkCmdClearColorImage` range. The Metal
extension reuses the original licensed kernels' mip/layer parameters and private encoder, dispatching the selected
shrunken mip dimensions. Existing mip-zero clear calls delegate to this path with their original selection.
Vulkan and Metal advertise the new capability; other backends leave it false and may expose a null entry.
The validation layer returns Unsupported for unavailable range clears. There is no emulated range-clear path.

Metal format queries now keep block-compressed formats sample/filter-only, check BC/ASTC device support, derive
vertex support from the implemented vertex mapping, and exclude integer blending. D24S8 maps to native D24S8 and
is unavailable when the device does not support it; it no longer silently becomes D32S8 while retaining a four-byte
texel size. Unsupported format/usage combinations return `Unsupported` before native texture allocation.
`test-storage-texture-clear` checks typed values, fractional normalized/sRGB conversion, complete array/3D contents,
untouched cells, explicit/remaining/whole ranges, invalid selections, failed recording and discard/retry. The
previous missing-clear failure is retained as negative evidence;
Metal graph history/reset now executes its data checks. These tests do not establish general stencil-view or heap
aliasing support.

Command allocator reset and destruction require the caller to establish completion first. Metal now owns its
command wrappers, abandons unsubmitted render/compute/blit encoders and releases the native command buffer instead
of overwriting a retained recording. Scratch and visibility buffers survive resets but are freed with the allocator.
The validation patch gives each allocator its own command wrappers and resets only their scope/thread state;
wrappers are freed at allocator destruction rather than retained until device teardown. A failed native begin does
not commit validation recording state. The `metal_command_allocator` and `command_allocator_ownership` configs
select fresh static-package identities, so rebuild and relink dependent consumers.

`test-frame-queues` uses caller-owned VRI fences for a graphics-producer, compute-consumer and transfer-readback chain.
Metal additionally submits the waiting consumer first, establishing a GPU-side wait without blocking the host.
The tests retain one submission in flight per Frame and explicitly restore tracked state after abandoned work.
The Metal regression also begins and discards 256 unsubmitted recordings before the exact fence-chain checks.
The `metal_command_autorelease` package identity selects the scoped acquisition fix. The original package blocks
while acquiring a native Metal command buffer in this stress test.
This is not proof of physical overlap, automatic graph scheduling, texture queue handoffs or general heap aliasing.
Vulkan queue types may refer to the same native queue; do not submit a reversed dependency on an aliased queue.

Texture, resize, odd-sized transfer and platform readback tests exercise these behaviors; they are not standards
conformance or headset acceptance. Remove each patch when a selected upstream commit includes its behavior.

`Device` accepts a VRI creation descriptor directly. Its convenience constructors explicitly select Y-up for the
existing builtin shaders/cameras; VRI's default descriptor still selects Vulkan conventions. Frame submission,
completion and explicit idle waits check their results. Destruction reports idle failure without throwing.

When rebuilding dependencies, preserve the configured version/backend flags: an unqualified package requirement
can select a different package. A changed static package requires relinking its consumers. Browser adapter/device
startup, native Linux WebGPU surfaces and hardware acceptance remain independent platform gates.

For Android Qualcomm depth arrays, the backend replaces a requested depth/stencil load clear with
[`vkCmdClearAttachments`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdClearAttachments.html)
over the same render area and rendered layers. This requires no transfer usage or additional queue submission.
A direct Vulkan control on Adreno 740 reproduces lost earlier depth/stencil values with load clears and preserves
all values with the explicit attachment clear. The workaround applies only to Android Qualcomm array textures;
ordinary 2D attachments and other platforms retain load clears. This is a correctness workaround, not a measured
frame-performance improvement. The texture-view regression covers D32 and D32S8, one and two layers, two mips,
separate sampled aspects and unchanged color/MSAA cases. The per-plane workaround honors explicit stencil operations independently.

Metal view-type conversion follows Apple's
[`newTextureView` contract](https://developer.apple.com/documentation/metal/mtltexture/newtextureviewwithpixelformat:texturetype:levels:slices:).
The MoltenVK multisample-array patch queries and enables the supported
[`VK_KHR_portability_subset` feature](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDevicePortabilitySubsetFeaturesKHR.html)
in the logical device, and rejects unsupported array creation before native calls. Both changes remain private
backend patches with no public VRI ABI addition.

Explicit stencil attachments take their clear value, load and store operations from the stencil slot. When both
slots are bound, use the same combined image view as required by
[`VkRenderingInfo`](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderingInfo.html). Omitting stencil
retains the existing combined-depth behavior. Stencil-only passes must disable depth testing and depth writes.
The readOnly depth convenience remains unchanged and cannot be combined with an explicit stencil slot.

MoltenVK 1.2.11's
[`getAttachmentInfo()`](https://github.com/KhronosGroup/MoltenVK/blob/v1.2.11/MoltenVK/MoltenVK/GPUObjects/MVKRenderPass.mm#L1086-L1106)
substitutes the supplied stencil attachment for a missing depth attachment when the underlying format supports
both planes. With a stencil clear, that also clears the unused depth plane. The macOS Vulkan patch binds a
preserving depth Load/Store attachment in this case and records the dependency required by that additional load.
It adds no queue submission. The regression verifies independent clear/load, stencil-only use, the existing
combined convenience and independent DontCare stores over two mips and one/two layers. It never asserts values
from discarded planes. Vultra now tracks separate aspect layouts where enabled; all-backend stencil conformance remains open.

Native Metal 1D textures and 1D arrays do not support mip chains. The dimensions patch returns Unsupported before
creating a Metal descriptor/resource for that combination. Its regression retains the original API validation
abort and an independent raw-VRI control: the prior package aborts for both types, while the patched package
returns Unsupported without a handle. Valid single-mip arrays still pass exact GPU transfer/readback. The new
`metal_texture_dimensions` package config gives consumers a fresh cache identity; it changes no public VRI ABI.

Independent depth/stencil state requires the enabled Vulkan `separateDepthStencilLayouts` feature, not only physical
support. Metal reports the capability because it has no native image-layout coupling. The appended device descriptor
field and expanded Vultra state/graph layouts require a full consumer rebuild. The new package configs distinguish
native patch revisions; common ABI/Vulkan patches apply without depending on macOS-only source changes.

Selected-plane buffer transfers use four bytes for D32S8/D24S8 depth and one for stencil. Metal image-to-image copies
of one combined plane use two buffer blits. MoltenVK 1.2.11's
[`MVKCmdCopyImage::encode()`](https://github.com/KhronosGroup/MoltenVK/blob/v1.2.11/MoltenVK/MoltenVK/Commands/MVKCmdTransfer.mm#L121-L275)
uses a direct Metal texture blit for equal formats without separating these aspects. The Vulkan workaround checks
`VK_DRIVER_ID_MOLTENVK` and records image-to-buffer, a transfer-write/read buffer dependency, then buffer-to-image.
Other Vulkan drivers keep the native image-copy path. Both implementations retain/reuse scratch per recording slot
until command-allocator destruction after caller-established completion. These copies cost extra GPU transfers and
scratch memory; they do not add a queue submission or CPU wait. Multisample partial copies return Unsupported from
EndCommandBuffer, propagated by Frame's Expected submission API before GPU submission; discard resets recording.
The D32S8 fixture checks two mips and layers, independent upload/readback, graph failure/discard/retry, selected
image copies preserving the other aspect and recording recovery. This does not establish D24S8 transfer, mixed
shader/attachment or comprehensive multisample depth/stencil conformance.

The descriptor-binding correction follows the distinct contracts of
[Vulkan binding flags](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorBindingFlagBits.html) and
[per-type feature requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutBindingFlagsCreateInfo.html).
Partial population does not itself grant update-after-bind or permission to overwrite a descriptor used by pending GPU work.

On Adreno 740, finite selected-layer 4x MSAA attachment resolves can leave zero or previous-generation values
without native validation errors, including with independent allocations. Ordinary and native-heap controls keep
all pixels, layers and sample counts. Different view types, transfer creation usages and an explicit attachment
clear do not repair that failure. A GPU memory dependency after each resolve does. The pinned Android patch records
that same dependency after a Qualcomm MSAA array resolve, before subsequent commands; it does not change layouts
or infer cross-queue waits. A matched native control removes only that dependency call and reproduces stale data.
Metal and other Vulkan devices keep their existing end-rendering path. This is finite correctness evidence, not
physical queue-overlap, scene performance or headset acceptance.

The `depth_stencil_sample_zero_resolve` package configuration identifies the expanded static ABI.
Rebuild callers for the appended depth and stencil capability fields. The VRI revision stays fixed.
Vulkan uses sample-zero resolve and retains the depth/stencil attachment layout. Metal selects native
resolve filters and store actions. Use the same source and target views in both slots of a combined
attachment. S8 uses the stencil slot. Check the capabilities before recording, including the load-only
capability for a scope with Load and no draws. Qualcomm Vulkan on Android requires a draw in such
scopes. Callers provide the resolve synchronization and retain the native views through completion.
[Vulkan render-pass resolve operations](https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-resolve-operations) and
[Metal GPU family capabilities](https://developer.apple.com/metal/capabilities/).

The `acceleration_scratch_rebuild` package configuration identifies the native rebuild correction. Release build
scratch only after its GPU use completes. A later ordinary build reacquires the original scratch capacity; it does
not resize the AS or authorize topology changes. Failed allocation prevents submission through EndCommandBuffer.
Discard the unsubmitted recording before retrying. The AS remains usable for the last completed build, but clients
that already changed its vertex contents must complete a successful rebuild before tracing that new geometry.
The scratch allocation remains owned until explicit completed-use release or AS destruction. Object listings grow
when it is reacquired and shrink when it is released; the AS object identity and debug name remain unchanged.

Metal now exposes completed-use scratch release through the existing ray interface, reacquires the original build
scratch length on a later build, and lists AS backing, scratch and translated instance buffers in object telemetry.
Allocation and encoder failures set the existing recording result returned by EndCommandBuffer. No public native
interface layout is expanded. Vulkan and Metal callers rebuild the selected package and all static consumers.
