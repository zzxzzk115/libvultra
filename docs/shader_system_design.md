# Internal Shader System Design

Status: proposed on 2026-10-06. This document defines the replacement design; it does not describe implemented
keyword support. Current runnable syntax and APIs remain in [shader_system.md](shader_system.md).

## Scope and baseline

Vultra owns this system internally. The experimental vshadersystem repository is useful evidence of requirements,
but is not a dependency, imported runtime, authoring contract or binary format. Keep one public Vultra library,
the existing VRI boundary, explicit application ownership and the existing RenderGraph executor.

The current implementation already provides Slang compilation, target reflection, typed game properties,
explicit named variants, raster Surface templates, cooking and transactional reload. Its variant loop compiles
each Pass for every named combination. A scoped ShaderCompiler now reuses checked primary-module IR across
entry/link selections and provides a validated disk program cache. Device owns an in-memory VRI graphics/compute
pipeline cache. It still does not implement typed keyword domains, usage-driven retention or selection-to-program
deduplication. These gaps must not be described as a complete engine shader system.

Retain two authoring paths:

| Path | Author owns | Vultra supplies |
| --- | --- | --- |
| Native .slang | Types, resources, entry points and VRI/graph execution | Optional specialization requests, compilation, reflection and cooking |
| Game .vshader | Properties, keyword declarations, Surface/custom Passes and render state | Material metadata, standard adapters, variant planning and binding |

Native research code does not require game metadata, a MaterialInstance, a SceneTree or engine attributes in Slang.
ANTLR parses only game descriptions. Ordinary Slang functions, modules, interfaces and generic types remain Slang.

## Keywords are typed selections

A keyword describes a finite static decision. Start with Boolean and Enum domains. Enum choices are exclusive;
Off is an explicit choice when needed. Unknown names, invalid values, contradictory selections, duplicate
declarations and incompatible bindings are errors. Persist names and enum labels, not transient bit positions.

Separate three decisions that the experimental model combined:

| Decision | Choices | Meaning |
| --- | --- | --- |
| Ownership | Material, Pipeline, Pass | Which explicit caller supplies the value |
| Compilation binding | LinkConstant, ModuleSelection, Preprocessor | How this static decision reaches Slang |
| Cooking policy | Used, AllLegal | Which legal selections the build must retain |

Material choices live on the instance; Pipeline choices belong to the application/renderer context; Pass choices
belong to its explicit invocation. There is no process-wide keyword registry or name-based global override.
One declaration has one owner. A renderer cannot silently overwrite a material choice with the same name.

Ordinary dynamic decisions remain Properties or host uniforms. Roughness, exposure, an animated tint and other
continuous values are not permutation domains. Dynamic branches do not contribute to a static key. Backend
specialization constants are a separate lowering mechanism and are deferred until VRI can represent and verify
the required pipeline creation inputs; metadata alone is not support.

The game language should make scope, finite values and lowering visible. The following is proposed syntax,
not a working example:

~~~text
Keywords
{
    Material Boolean NORMAL_MAP = false LinkConstant
    Material Enum QUALITY { Low, Medium, High } = Medium LinkConstant
    Pipeline Enum SHADOWS { Off, PCF, PCSS } = PCF LinkConstant
}
~~~

The parser produces typed declarations with source locations. A constant declaration emits an ordinary Slang
extern/export binding with a reserved Vultra prefix; enum labels produce checked integer values and named
constant aliases. An explicit binding can target a native Slang symbol. Preprocessor binding is opt-in and
places values in the front-end key; it is appropriate for syntax/entry declarations that must change before
Slang parses the module. Type/interface choices select ordinary Slang modules and conformances, not a second
type language.

No keyword is inferred solely from texture presence. A keyword can control whether an active Pass requires a
texture, but changing an AssetId does not silently change shader semantics.

## Legal combinations and named presets

Keep named Variant declarations as presets of typed selections and module bindings. A preset is not the
identity of compiled code. Two presets resolving to the same complete selection share the same program.

Constraints use a small typed expression grammar: names, finite values, equality/inequality, Boolean operators
and implication. Parse and validate expressions once; never keep an unchecked expression string for runtime
interpretation. Capability requirements are evaluated separately from logical constraints.

For example, a declaration may reject QUALITY = Low together with an enabled expensive lobe. Device capabilities
can rule out a mesh entry. Requested illegal combinations fail with the keyword values, constraint and original
location. They do not choose a nearby combination or silently revert to defaults.

Defaults form one complete selection and must themselves be legal. Sparse caller selections are completed with
declared defaults. A preset conflicting with an explicit override reports the conflict unless the API explicitly
applies the preset first and then validates a separate override transaction.

A keyword schema owns stable name/value identities and a schema digest. Runtime handles are scoped to that schema
and invalidated by a new publication. Declaration reordering does not change a logical key. Do not fix correctness
to a machine-endian 64-bit name hash or a fixed keyword bit limit: retain canonical names/values for collision
checks, serialization and diagnostics, and use a compact internal encoding only as an implementation detail.

## Selection, programs, layouts and pipelines are different identities

| Identity | Contains | Excludes |
| --- | --- | --- |
| Selection key | Schema identity and completed typed keyword values | Human preset name, uniform values |
| Front-end module key | Actual source/dependency bytes, resolved imports/search roots, macros and Slang/compiler options | Link-only constants and draw state |
| Program key | Module identities, linked constants/types/modules, entry group, target/profile/capabilities and adapter contract | Material uniform values and attachment state |
| Layout identity | Actual target reflection, resource categories/counts and cumulative offsets/strides | Guessed property order |
| Pipeline key | Program/layout identities and normalized VRI state, attachments, samples and vertex/mesh contract | Unrelated keywords and uniform updates |

A hash is a lookup accelerator; equality compares the canonical payload. Use an explicit serialized byte order.
AssetId identifies the asset, while content identity identifies the compiled revision.

Properties describe the logical material schema. Every linked program retains its actual reflected layout.
Two variants may have different active resources and offsets. Uniform buffers and descriptor sets are built for
the selected layout; an inactive resource is not fabricated into reflection. Required-resource checks apply to
the selected program's contract. Runtime descriptor allocation and writes use reflected array counts, matrix
major order/stride, sets and binding offsets.

A pipeline state property changes the pipeline key, not shader code. A uniform edit changes uploaded bytes.
A static keyword selects a program and possibly a new layout. Those three operations must have distinct logs,
revision handling and performance tests.

## Conservative Pass and stage handling

One material selection feeds Forward, both G-buffer groups, depth and shadow evaluation. Alpha coverage,
deformation, mirrored transforms and double-sided behavior cannot diverge because a Pass omitted a keyword.

Initially resolve the full selection for every relevant Pass. Reuse identical emitted programs/reflection after
compilation; this safely deduplicates a normal-map choice that optimization removes from a shadow program.
Pass/stage projections are a later optimization, requiring proven dependencies. A handwritten stage hint or a
text search cannot prove that an imported helper, resource layout or varying interface is unaffected.

A projected key must retain everything affecting the full linked entry group and its interface. If dependency
information is unavailable, keep the complete key. Never advertise Vulkan stage-specific savings before measuring
actual Slang output and linkage. Standard Surface and custom Passes use this same rule.

Attachment formats, graph resources, barriers and execution order remain caller-owned. SubShader/Pass capability
selection and VRI contract validation happen before GPU creation. Keywords do not allocate graph resources or
implicitly install graph passes.

## Compiler ownership and module reuse

The first compiler slice is implemented in ShaderCompiler. One game asset compilation shares a context across
its Passes/variants; source-backed native ShaderPipeline retains a context across reloads. Explicit native users
can retain their own context. Slang handles remain private, and published programs own their code/reflection.

A context holds one lazy global session and at most 16 serialized primary-module IR snapshots in memory. Every
request creates an isolated Slang session. Matching IR is loaded before linking constants or extra modules;
changed macros create a different frontend partition. GPU readbacks verify different link constants, unchanged
target reflection, changed include resolution, macro isolation and source-free game variants with the pinned
compiler. Linked modules and composed entry groups are not yet separately reused. Concurrent callers must own
separate contexts; there is no shared mutable session across vtask workers.

A checksummed development target-program cache validates the full canonical request, dependency contents and
include resolution before returning bytecode/reflection without initializing Slang. Cache failures recook while
runtime artifact failures remain errors. Device supplies an in-memory driver cache to graphics/compute descriptors.
Neither module IR nor driver caches are currently persisted; ray-tracing descriptors do not expose driver caching.
The runnable contracts and statistics are documented in shader_system.md.

Future work may separately persist reusable IR and driver artifacts after measuring their value and validating
compiler/target/driver identity, bounds and recovery. Logical-versus-unique program counts and keyword-aware
deduplication remain part of the proposed domain/cooking gates. A cache hit must never depend only on file times
or a logical source name.

## Cooking and packaged coverage

The cooker builds an explicit plan:

1. Validate schemas, defaults, constraints, presets and program contracts.
2. Gather material selections from packaged scenes/materials.
3. Merge explicitly declared application/script/runtime selections.
4. Expand AllLegal domains within an explicit project budget; retain Used selections from the gathered plan.
5. Apply device/target restrictions and explain every rejected selection.
6. Compile retained programs, deduplicate exact code/layout results and publish atomically.

Material scanning cannot prove which states C++, Lua, Python or C# will request later. Runtime-switchable
selections must appear in the build manifest or use AllLegal retention. Recorded editor usage can seed a manifest,
but does not establish complete coverage.

Compute theoretical/retained counts with overflow checks before creating jobs. Enforce configurable compile
budgets with a diagnostic showing the domains responsible for growth. Do not silently prune a valid requested
selection when the budget is exceeded.

A cooked game asset stores the schema, defaults, presets, constraints/validated selection records, selected
SubShaders/Pass contracts and an exact selection-to-program mapping. Native artifacts contain the explicitly
requested programs without game material semantics. Both retain target reflection, capabilities and dependency
summaries. Continue version 1 and deliberately update all readers, writers, packers, callers and tests; do not add
a format-probing compatibility loader or import experimental vshadersystem formats.

Shipping startup loads target code through AssetSource, without source, Slang frontend or ANTLR parsing.
Requesting an absent selection fails with shader, Pass, target and resolved values plus the missing cooking rule.
No shipping runtime compilation, best-match lookup or unlogged replacement program is allowed.

## Runtime transitions and hot reload

Material keyword updates are validated as a transaction. Before changing a live selection, resolve every required
Pass, validate reflected resources/contracts and prepare candidate GPU buffers, descriptors and pipelines.
Publish the complete material binding at a completed GPU frame boundary. A failed selection leaves the previous
valid selection and GPU output intact; the caller receives an error and chooses its application policy.

A source reload publishes an entire asset revision and all affected instances. Compile candidates off-thread;
GPU preparation and publication remain on the main thread. Discard candidates if source, keyword request or
render contract revisions change. Cancel/coalesce obsolete work without allowing an old job to publish.

Preserve keyword values only when name, kind, owner and enum label remain compatible. Added declarations use
defaults; removed/changed declarations reset with specific diagnostics. Revalidate constraints and packaged
coverage. Preserve compatible numeric/resource overrides separately. Never rewrite material files or reupload
geometry as a consequence of shader publication.

Editor requests for missing development variants may schedule explicit compilation while retaining the old live
binding. This is a development workflow, not a per-frame lazy compilation path. Stable frames must not compile,
create pipelines or hash the entire keyword domain for every draw.

## Inspector, language service and research API

The Inspector edits exclusive enum groups and Boolean keywords separately from uniform properties. It shows
owner, selected preset, resolved values, whether each selection is compiled/packaged, and reset actions. Pipeline
and Pass-owned choices are displayed as caller-owned controls, not mutable material fields.

Generated Slang projections include checked keyword declarations and the selected specialization. Completion,
definition and diagnostics map back to keyword declarations or editable Slang blocks. Source maps and document
revisions cover schema edits as well as program edits. Raw Slang retains its native extension.

Native applications can supply a finite specialization domain and compile a selection directly with their
ShaderCompileOptions, or continue the existing direct ShaderProgram/ShaderPipeline API without a domain.
Scientific sweeps own their output manifests, explicit selections and execution. Keyword support must not force
a material Inspector, renderer, scene or VPK into a compute experiment.

## Implementation gates

| Gate | Runnable result | Required evidence |
| --- | --- | --- |
| Domain | Typed keyword schema/key/constraints and named presets, without changing rendering | Invalid/default/exclusive/overflow cases; deterministic keys; collisions and reorder tests |
| Compilation | Scoped module reuse and native/game lowering | Macro partition versus link-only reuse; actual reflection and timing; module/type specialization errors |
| Cooking | Retained selection manifest and exact packaged lookup | Dynamic application states; budget failures; deterministic stripping/dedup; source-free native/game VPK readback |
| Rendering | Exact program/layout/PSO selection in Surface/custom Passes | Forward/deferred/depth/shadow/mesh consistency; resource layout changes; uniform/state/keyword separation |
| Workflow | Transactional selection/reload and mapped Inspector/services | Compile/pipeline/resource failures; rapid edits; schema changes; zero stable-frame compilation; retained geometry |

Replace the old named-string program map as these gates land; do not keep parallel independent shader runtimes.
Keep the runnable current implementation until its replacement has observable parity. Builtin materials, custom
compute and raw draw/mesh/RayQuery remain regression inputs. Broader backend and automatic game path-tracing
support require their own verified contracts.

## References

Unity distinguishes compile-time variants, dynamic branching, keyword scope and cooking retention; that is useful
behavioral context, not a source format or API to import:
[Declare shader keywords](https://docs.unity.com/en-us/engine/6000.3/manual/materials-and-shaders/shaders/writing-custom/shader-writing/sl-multiple-program-variants/declare).

Slang documents reusable module precompilation and link-time constant/type specialization:
[Link-time specialization](https://shader-slang.org/slang/user-guide/link-time-specialization).
The private compilation context follows its component/session model:
[Compilation API](https://docs.shader-slang.org/en/latest/compilation-api.html).
