# Roadmap

Develop a small, composable C++23 graphics library. Direct VRI programs, code-driven RenderGraph applications and
optional shader/asset/UI features share one public `vultra` static library. [architecture.md](architecture.md)
defines ownership and subsystem boundaries; [BUILD.md](../BUILD.md) documents actual targets and verification.
All serialized-data, file-format and ABI versions remain **1** before release, with deliberate breaking changes.

## Priorities

| Order | Deliverable | Acceptance |
| --- | --- | --- |
| 0 — RenderGraph foundations | Complete the graph contracts and compiler gates below before expanding application workbenches or algorithm collections. | General resource/port descriptions, explicit compile/invalidation stages and independently verified graph execution. A fixed renderer followed by postprocessing does not establish this gate. |
| 1 — dependency boundaries | One selected source/package closure; foundation, GPU, graph, shader runtime, assets and UI remain independently selectable. Keep source/tool/test ownership isolated. | Independent direct-VRI and RenderGraph clients compile without ECS, VM, importer or UI. Basic examples require only the capabilities they demonstrate. Each selected module has its own observable contract tests. |
| 2 — reusable runtime | Finish platform/resource ownership, prepared asset/texture upload and cooked shader/renderer separation from host compiler/import/watch services. | Direct/graph/offline paths share VRI semantics. Verify GPU final-use teardown, failed reload and explicit capability errors. Cooked paths run without host tools; check layouts, UV/depth and final/intermediate images. |
| 3 — properties and C ABI | Normalize the existing libclang IR/marshalling model; generate descriptors, C tables and typed SDK access from annotated declarations. | One supported property/method addition needs no per-function emitter or wrapper edit. Check source diagnostics, layouts/fingerprints, ownership/borrows, wrong context/stale IDs, independent C callers and deterministic output. |
| 4 — graph and diagnostics | Stabilize PassCatalog/graph contracts, material blocks, resource/history lifetime and profiling/capture through the existing executor. | Direct C++/JSON graphs agree; invalid graphs fail before recording, failed replacement preserves valid output, history resets correctly and captures identify producing passes/resources. |
| 5 — native GUI | Optional retained controls, drawing/text and PropertyPresenter over generic descriptors; application documents own commands/history. | Controls and Inspector work independently of ECS, ImGui/RmlUi or .NET. Verify live change/commit/cancel, focus, pointer capture, deletion during dispatch, text/IME, DPI and multiple contexts. |
| 6 — delivery | External consumption, explicit dependency closures, generic VPK/cooking tools and supported backend/platform contracts. | Selected configurations and copied built tools run without a source checkout or xmake at runtime. Verify clean Linux/Windows delivery and desktop/Web behavior separately; mobile initially requires compile/link. |

Use small standalone programs to validate these boundaries. Add reusable mechanisms for concrete callers rather
than introducing an engine object model, application workflow or general plugin framework into the library.

## Current gaps

- Engine/script/research implementations and their tests live with their owner. Finish consumer integration before advancing submodule pins.
- Cooked rendering/VGui now build without host compilation/import services and run from VPK shader entries. Broader platform delivery remains unverified.
- The shared generator now owns domain discovery, transport/borrow metadata and service-table fingerprints. Host-specific SDK roles and safe class/member generation remain incomplete; C layouts alone do not complete a typed SDK.
- Native retained GUI is not implemented. Current UI support is EditorGui/ImGui and VGui/RmlUi; advanced RmlUi backend operations remain unsupported.
- Local Linux/offline and focused platform probes provide evidence; complete Windows, clean-machine and hardware-device acceptance remains separate.

Preserve working behavior and regression coverage while resolving these gaps. Verification status does not turn
an incomplete implementation into a supported platform or API contract.

## RenderGraph prerequisite gates

The graph compiler is a library priority. Application tools consume these mechanisms after their standalone
regressions pass; adding more passes or a node canvas does not substitute for a complete resource contract.
Keep direct `addPass()`, VRI descriptors and the existing executor throughout this work.

| Gate | Required mechanism and acceptance |
| --- | --- |
| RG0 — compilation correctness | Separate content liveness from RAW/WAR/WAW execution ordering. Full overwrite discards old content; read/write retains it. Retain explicit side effects and only the final observable history write. Resolve definition reachability before creating GPU pass implementations, without hiding malformed schema/cycles or skipping live capability checks. Verify captured earlier values, failed candidates and aliasing parity. |
| RG1 — resource and port contracts | Describe texture dimension, mip/layer/sample counts, usage and size relations; buffer byte size/stride; optional inputs and internal resources. Resolve connection constraints before GPU initialization. Add subresource ranges with matching view, initialization and VRI barrier tracking, rather than merely removing the current 2D restriction. Verify a mip pyramid, array/cube and volume data, MSAA/explicit resolve, and rejected incompatible connections. |
| RG2 — pass compilation lifecycle | Separate cheap declarations, constraint resolution and expensive pipeline/resource preparation. Represent explicit execution edges independently of resource connections. Port-affecting changes request graph recompilation; unchanged resources and ordinary value edits do not recreate the graph. Define ownership of pass instances, callbacks and the compiled plan. Failed candidates retain the preceding executable graph. Keep one executor, with no renderer/world required. |
| RG3 — typed parameters and invalidation | Reuse generic property values for bool/integer/float/enum/vector and structured parameters. Classify edits that update constants, rebuild pipelines/resources/topology or reset history. Test immediate parameter changes, invalid edits, resize/shader changes and independent graph instances. Numeric slider metadata alone does not establish this gate. |
| RG4 — diagnostics and backend acceptance | Explain why each pass/resource is active or culled, distinguish data and ordering edges, and associate resources/history/allocations with timings and captures. Verify C++/JSON output parity, finite Vulkan runs and separately supported backends. Whole-resource reuse is not heap aliasing; asynchronous queues remain a separate verified VRI capability. |

The RG0 slice passes focused liveness, side-effect, partial-write and early-pruning regressions. RG1–RG3 remain open; the current graph must not be described as matching
Falcor's general pass/resource composition before these gates pass. Interactive authoring and experiment policy
belong to applications; they must exercise the same compiler as direct C++ and offline execution.
Reference contracts: Falcor's [resource reflection](https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/Falcor/RenderGraph/RenderPassReflection.h)
and [compiler](https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/Falcor/RenderGraph/RenderGraphCompiler.cpp).

## Platform gates

VRI remains the only backend boundary. Android/GameActivity and browser hosts reuse the application phases,
resource parser and cooked shaders. Desktop/Web work has priority; Android/iOS/iPadOS initially need ARM64
compile/link coverage. A delivered APK/Web sample embeds its VPK and needs no external pack or whole-pack extraction.

| Gate | Implementation and acceptance |
| --- | --- |
| P0 — embedded resources | One version-1 VPK parser for files/borrowed immutable bytes. Map uncompressed APK assets and link Web packs into WASM. Verify checksums/path validation, packed shader access and finite-frame pixel readback. |
| P1 — host lifecycle | GameActivity/JNI surface loss/recreation, pause/resume and GPU-safe teardown. Browser asynchronous initialization, animation frames, canvas resize/DPR, visibility and device loss. Preserve update/UI/record/submit phases. |
| P2 — shaders/capabilities | Host-cook SPIR-V/WGSL through Slang. WebGL2 accepts only its supported subset; unavailable stages/formats/interfaces fail explicitly. Check matrix packing, bindings, UV/depth/orientation and image output on each selected backend. |
| P3 — XR integration | Android OpenXR loader/context/session integration and VRI image interop. Validate final-use completion, activity/session transitions and view/input submission on real hardware. Compilation or simulation does not validate a headset. |
| P4 — packaging/regression | Built APIs/tools package APK/WASM resources without xmake at runtime. Validate asset storage/alignment, offline startup, Linux/Windows direct C++ samples and embedded executable resources. |

### Platform evidence and open work

| Area | Existing evidence | Still open |
| --- | --- | --- |
| Embedded data | File/byte reads, APK mapping and WASM embedding; ARM64 APK compilation/signing/alignment and x86_64 emulator pixel tests. | Clean-machine delivery and real mobile/headset acceptance. |
| Lifecycle | Shared RenderApp phases, GameActivity pause/recreation/resize and WebGL2 frame/DPR/canvas readback tests. | Repeatable WebGPU startup/device loss, GUI/IME and gamepad coverage. |
| Cooked rendering | SPIR-V/WGSL direct/graph triangles and shared-Slang transform/texture/depth/compute probes; cooked material replacement/failure recovery on Vulkan/WebGPU/WebGL2. | Broader layouts/formats, full renderer runtime isolation and advanced paths. Queried RT/Mesh flags do not prove execution. |
| Tools | Android packaging consumes built libraries/VPK; Web examples embed resources. | Portable GUI and full cooked rendering closure. |

The `rendering` closure consumes prepared uploads and cooked shaders without host compilation or import services.
Independent Linux forward/deferred/VGui probes verify this boundary; repeat the complete closure on other platforms.
Existing [platform probes](../tests/platform/xmake.lua)
retain CPU-reference pixels, reversed draw-order depth checks and explicit capability-selection diagnostics.

| Example | Acceptance focus |
| --- | --- |
| `example-basics` | Window/clear, direct VRI, RenderGraph and capability-gated mesh shading. |
| `example-offline` | Indexed triangle, HDR/compute, C++/JSON parity, display conversion and readback; host-cooked APK/VPK shader loading. |
| `example-ui` | Raw ImGui, EditorGui and VGui interaction/drawing/lifecycle on supported hosts. |

Additional rendering-data probes should cover cubemap/IBL, forward/deferred PBR, comparison-sampled shadows and
tone mapping. Query granted RT/Ray Query/Mesh/Compute interfaces, formats and limits before selecting pipelines.
An explicitly selected simpler algorithm needs its own pixel test; shader/device/validation failures are not
capability fallbacks. Browser software readbacks and Android emulator runs do not establish hardware execution.

Platform references: [Android assets](https://developer.android.com/ndk/reference/group/asset),
[Emscripten embedding](https://emscripten.org/docs/porting/files/packaging_files.html),
[OpenXR Android initialization](https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/main/src/tests/hello_xr/main.cpp)
and [xtool](https://github.com/xtool-org/xtool). iOS linkage remains unverified; xtool is a host tool, not a runtime dependency.

## Shader and material system

Keep native Slang and optional `.vshader` metadata. MaterialBlock separates reflected layout, typed CPU instance
values and GPU storage/bindings; OpenPBR is one builtin schema. Plain Slang use must not require a scene or builtin
renderer. Reuse allocations for value changes within a layout and validate each pass's material inputs. An unlit
block and a distinct custom block must support persistence, inspection, live edits and type/layout rejection with
image readback before this is complete. MaterialInstance/ShaderMaterial are the existing implementation base.

Add finite Bool/Enum keywords with explicit material/pipeline/pass ownership, legal-selection validation and named
presets. Keep selection/program/layout/pipeline identities separate. Cooking retains used or all-legal combinations
explicitly and deduplicates equivalent programs. Dynamic values remain uniforms; no global keyword state.

Use the [vshadersystem Slang implementation](https://github.com/zzxzzk115/vshadersystem/tree/cf2ea375a93ab8d641aefb2cd9e2398d1764e804)
as a mechanism reference, not a dependency or execution layer:

| Mechanism | Acceptance |
| --- | --- |
| Cooker/runtime separation | Load/bind and transactionally replace cooked materials without Slang, ANTLR, file watchers or importers; retain Vulkan/WebGPU/WebGL2 image checks. |
| Target payloads | Cook SPIR-V/WGSL from one Slang source; add DXIL with verified backend selection. Strip declared targets explicitly and validate target-specific bindings, packing and push constants. |
| Variants | Bounded Bool/Enum expansion, constraints and used/all-legal policy. Reflect each variant because bindings may change. Verify archive round trips and actual layout compatibility. |
| Material metadata | Evaluate Slang user attributes for defaults/ranges/labels using the same property model. Preserve plain Slang and existing `.vshader` authoring. |
| Profiling artifacts | Select optimization and source/line debug information independently of C++ build mode; include both in cache identity and inspect a real GPU capture. |
| Incremental cooking | Content/dependency caches include targets/variants and import resolution; preserve deterministic output, unchanged timestamps and the previous valid artifact after failure. |

## Extension boundaries and verification

Project passes install into explicit catalogs. Vendor SDK types and neural/backend instructions do not become
another RHI or mandatory core dependency; native interop uses declared VRI interfaces. Application documents,
simulation, language-host lifecycle, experiment reports and vendor algorithms are outside library policy.

Each gate needs affected builds/tests, deterministic codegen, formatting/static analysis and failure/recovery
coverage. Rendering additionally needs finite GPU diagnostics and image readback. Prefer offline QA; explicit
visible testing uses Workspace 5. Documentation-only changes do not launch GPU tests. VRI's device conventions
and submit/wait results are already adopted; its API release version is separate from version-1 data/ABI formats.
