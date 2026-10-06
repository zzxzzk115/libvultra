# Vultra Shader

This guide describes the current implementation. The proposed internal keyword and specialization design is in
[Internal Shader System Design](shader_system_design.md); those extensions are not implemented yet.

Vultra has two independent authoring paths. A game material is a `.vshader` file with a ShaderLab-style
description and ordinary Slang program blocks. A research program is a native `.slang` file; its host constructs
VRI descriptors and records draw or dispatch commands directly. Neither path requires a SceneTree. Both use
`ShaderProgram` for compilation, SPIR-V reflection, dependency tracking and checked cooking.

This implementation targets Vulkan/SPIR-V. Other backend layouts have not been validated. The game language
borrows Unity's `Shader`, `Properties`, `SubShader`, `Pass` and `Tags` structure, but does not import Unity shaders
or implement HLSLPROGRAM, Unity macros, keyword products or Unity's surface expression language.

## Start with a runnable example

```powershell
xmake run example-shader --frames 60
xmake run example-shader --deferred --frames 60
xmake run example-shader --meshlets --frames 60
xmake run example-shader --edit
```

`--meshlets` selects Forward task/mesh drawing; `--deferred` selects indexed G-buffer drawing. The current
renderer rejects their combination. Mesh-driven G-buffer rendering remains unimplemented.

`example-shader` depends on `example-shaders`. This build target cooks the game example and a native research
compute shader before execution, reusing unchanged artifacts. The default game example loads
`build/shaders/examples/painted_metal.vshaderc`; `--edit` instead loads the source and enables FileWatch reload.
The example uses scoped assets and instances directly, without a SceneTree. Its Inspector edits one sphere's
properties while the renderer retains geometry and cached pipelines. Use `--capture file.png` for the last
finite frame and `--layout-file file.ini` to isolate the example layout. Cull is a typed state property; returning
to a previously used value selects its cached pipeline.

Native examples remain available through `example-basics`, `example-research` and `example-ray`. Research
programs retain their own resource layouts and execution model.

## Game source

```text
Shader "Examples/PaintedMetal"
{
    Properties
    {
        baseColor ("Base Color", Color) = (0.8, 0.2, 0.1, 1)
        roughness ("Roughness", Range(0.03, 1)) = 0.4
        metalness ("Metalness", Range(0, 1)) = 0.8
        [Normal] normalMap ("Normal", 2D) = "normal" {}
    }

    SubShader
    {
        Tags { "RenderPipeline" = "Vultra" "RenderType" = "Opaque" }
        Cull Back
        ZWrite On
        Surface
        {
            Model OpenPBR
            Entry surfaceMain
            SLANGPROGRAM
            void surfaceMain(SurfaceInput input, inout SurfaceOutput output)
            {
                output.baseColor = material.baseColor.rgb;
                output.specularRoughness = material.roughness;
                output.metalness = material.metalness;
                output.tangentSpaceNormal = decodeSurfaceNormal(
                    material.normalMap.Sample(material.normalMapSampler, input.uv),
                    material.normalMapEncoding);
            }
            ENDSLANG
        }
    }
}
```

Keywords are case sensitive. Outer declarations may use optional semicolons, `//` comments and `/* */` comments.
`SLANGPROGRAM` and `SLANGINCLUDE` start on a line ending after the keyword. `ENDSLANG` ends a program only when
it occupies a complete line outside a string, character literal, comment or continued preprocessor directive.
ANTLR parses this outer structure; lexer modes emit native text chunks instead of one token per character.
Slang owns the program's syntax, include/import behavior and type checking.

### Properties and instances

`Properties` is the sole declaration of game material parameters. Access each property as `material.name`.
Every declaration contains an identifier, Inspector label, type and typed default.

| Type | Default | Inspector / upload |
| --- | --- | --- |
| `Float` | finite scalar | Drag control / 32-bit float |
| `Range(min, max)` | finite scalar | Slider; scripts are not clamped |
| `Integer` | signed 32-bit integer | Integer control or explicit enum |
| `Boolean` | `true` or `false` | Checkbox / reflected Slang bool storage |
| `Vector` | four finite components | Vector control |
| `Color` | four finite components | sRGB RGB editing, converted to linear RGB before upload; alpha stays linear |
| `2D`, `2DArray`, `3D`, `Cube`, `CubeArray` | texture default | Typed texture and corresponding sampler |

Known attributes are `[HDR]`, `[Normal]`, `[HideInInspector]`, `[NoScaleOffset]`,
`[Enum(Label, integer, OtherLabel, integer)]`, `[SRGB]` and `[Linear]`. Unknown attributes fail at their source
location. HDR colors use scene-linear values. Texture properties are linear data by default; `[SRGB]` selects
color sampling and `[Normal]` selects linear normal data. Conflicting uses are rejected. Texture resource
dimensions must match the declaration.

Texture defaults are `"white" {}`, `"black" {}` and `"normal" {}`. The normal default is exactly `(0.5, 0.5, 1)`.
`"" {}` declares a required binding. A missing required texture prevents material preparation. Scale/offset is
exposed as `material.nameScaleOffset` for 2D and array properties unless `[NoScaleOffset]` is present; the author
chooses where to apply it. A normal property also exposes `material.nameEncoding` for `decodeSurfaceNormal`.

`MaterialInstance` stores the shader's AssetId, a named variant and typed overrides. Defaults stay in the
ShaderAsset. `set`, `reset`, `setVariant` and `assign` update its revision only when needed. JSON persistence is
version 1. Invalid types and non-finite values fail explicitly; Range bounds control the Inspector, not scripts.

CPU writes use the target's reflected uniform offsets and size. Reflected arrays expose element strides;
matrices expose row/column orientation and major-vector strides. Resource arrays retain their count and binding.
Descriptor sets are positional, including empty sets between used indices. Material layouts bound host allocations
to set indices 0–31; actual device limits still apply. There is no property-order layout calculation.

### Standard Surface contract

`SurfaceInput` contains world position, geometric normal, tangent and handedness, UV, vertex color and front-face
information. `SurfaceOutput` is initialized before the author's function runs:

| Field | Default |
| --- | --- |
| `baseColor`, `baseWeight`, `metalness` | white, 1, 0 |
| `specularRoughness`, `baseDiffuseRoughness` | 0.5, 0 |
| `specularColor`, `specularWeight`, `specularIor` | white, 1, 1.5 |
| `coatWeight`, `coatRoughness`, `coatIor` | 0, 0.03, 1.5 |
| `emission`, `occlusion` | black, 1 |
| `tangentSpaceNormal` | `(0, 0, 1)` |
| `opacity`, `alphaCutoff` | 1, 0.5 |

One function generates Forward, GBufferBase, GBufferMaterial, DepthOnly and ShadowCaster, plus the MeshForward
task/mesh entry. MeshForward reuses the native renderer's meshlet entry implementation. Shared templates handle
normal mapping, mirrored transforms, back faces and alpha-mask coverage. `RenderType "AlphaTest"` enables the
same opacity/cutoff test for color, depth and shadow passes. `Opaque` does not discard based on opacity.

This contract implements the renderer's opaque OpenPBR base/specular/coat subset, not full OpenPBR conformance.
Game Surface functions do not automatically participate in the reference path tracer. Research RayQuery programs
remain native Slang, and SceneTree game materials explicitly require a raster path.

### Explicit passes and render state

```text
Pass
{
    Name "Forward"
    Tags { "LightMode" = "Forward" }
    Vertex vertexMain
    Fragment fragmentMain
    Cull Back
    ZWrite On
    ZTest LEqual
    SLANGPROGRAM
    // Ordinary Slang entry points, types and helper functions.
    ENDSLANG
}
```

Valid stage combinations are Vertex/Fragment, Mesh/Fragment, Task/Mesh/Fragment and Compute. A vertex-only pass
must be DepthOnly or ShadowCaster. Compute uses Custom LightMode and does not inherit graphics state.
`SLANGINCLUDE` at SubShader scope supplies shared native declarations to each pass. Missing entries and conflicting
stages identify their original source location.

LightMode values are Forward, DepthOnly, ShadowCaster, GBufferBase, GBufferMaterial and Custom. Explicit passes
replace Surface-generated passes of the same standard purpose. Duplicate explicit standard purposes fail;
multiple Custom passes must have distinct names. A custom pipeline used by BuiltinRenderer must implement its
vertex/resource/attachment contract and 32-byte GameDrawData push constants. Arbitrary custom compute or graphics
passes can instead use ShaderMaterial directly with an explicit ShaderPassContext and resource views.

SubShader state is inherited and Pass state overrides it. The implemented commands map to VRI descriptors:

| Command | Values |
| --- | --- |
| `Cull` | Off, Front, Back |
| `FrontFace` | CCW, CW |
| `ZWrite` | On, Off |
| `ZTest` | Never, Less, Equal, LEqual, Greater, NotEqual, GEqual, Always |
| `DepthBias` | constant, slope, optional clamp |
| `Blend` | Off, or source/destination color factors, optionally `,` source/destination alpha factors |
| `BlendOp` | Add, Subtract, ReverseSubtract, Min, Max; optional separate alpha operation |
| `ColorMask` | unique RGBA channels or 0; optional attachment index |
| `Stencil { ... }` | Ref, ReadMask, WriteMask, Comp, Pass, Fail, ZFail |

Blend factors are Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor, SrcAlpha,
OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha, ConstantColor, OneMinusConstantColor, ConstantAlpha,
OneMinusConstantAlpha and SrcAlphaSaturate. Stencil operations are Keep, Zero, Replace, IncrSat, DecrSat,
Invert, IncrWrap and DecrWrap. Common stencil settings apply to both faces. Stencil requires a stencil-capable
attachment; depth state requires a depth attachment.

Use `[propertyName]` to reference a type-compatible property from state. Cull and other enums use integer indices
in the VRI ordering; Boolean states require Boolean properties. DepthBias accepts Float/Range/Integer; masks and
stencil integers require Integer. State changes select cached pipelines without recompiling Slang. Properties
which affect only uniform values retain their pipeline. Shader files do not allocate RenderGraph resources,
declare barriers or determine execution order: the caller owns attachments, input streams and scheduling.

### SubShaders and finite variants

The first SubShader satisfying its RenderPipeline, required LightModes, enabled device features and the host
compatibility callback is selected. Diagnostics list the selected index and rejection reasons. The builtin renderer
checks each cooked variant against its fixed attachment counts and 32-byte vultraDraw ABI; direct callers can
provide their own compatibility check through ShaderAsset::SubshaderCompatibility. `Requires MeshShader` or `Requires RayQuery` may appear
on a SubShader or Pass. Native Slang target capabilities remain explicit compile options.

Variants are named combinations at Shader scope; there is no keyword Cartesian product:

```text
Variant "Default"
{
    Constants { float detailWeight = 0.5 bool useDetail = true }
    Modules { "detail.slang" }
    Defines { DETAIL_SAMPLES = 4 }
}
```

Constants accept bool, int, uint and float. Declare their corresponding native `extern static const` symbols in
Slang and link the named modules; Slang supplies module/type specialization. If no variants are written, the
language supplies Default. Cooking includes all explicit variants unless repeated `--variant` options select a
subset. A runtime request for an absent cooked variant fails. The source-free artifact omits program text and
authoring-only link declarations.

## Native research compilation

The existing `ShaderPipeline(device, file, entries, builder, watchRoot, includeRoots)` interface remains available.
Its builder receives ordinary VriShaderDesc values. A second constructor accepts ShaderCompileOptions with explicit
entries, ordered include roots, link modules, generated link sources, defines, SPIR-V profile, ray-query and target
capabilities. ShaderProgram supports the same options for compilation without a pipeline.

The experiment owns layouts, descriptor pools, bindings and draw/dispatch commands. Uniform buffers, thread group
sizes, mesh outputs and native language features come from Slang and reflection. No game material declaration,
shader registry or SceneTree is introduced on this path.

## Compilation cost and development caches

Use a retained ShaderCompiler for native experiments that compile several entry groups or link-time constants:

~~~cpp
#include <vultra/drivers/rhi/shader_compiler.hpp>

vultra::ShaderCompiler compiler;
vultra::ShaderCompileOptions options;
options.entries = {{"main", VriShaderStage_Compute}};
options.linkSources = {{"selection", "export static const uint samples = 16;"}};
auto program = compiler.compile("experiment.slang", options);

options.linkSources.front().value = "export static const uint samples = 64;";
auto specialized = compiler.compile("experiment.slang", options);
const auto timings = compiler.lastCompileStatistics();
~~~

A context owns one lazily initialized Slang global session and up to 16 checked module IR snapshots. Each request
uses a fresh compilation session: entry selection and link constants cannot leak between variants. Matching primary
module IR can be reused for another entry group or link constant; changed macros, profiles, capabilities, source or
resolved dependencies require new frontend work. Linked modules are compiled in that isolated session. One caller
owns a context; concurrent workers must use separate contexts. The ordinary ShaderProgram convenience functions
remain available. Source-backed ShaderPipeline retains its own context across reloads; cooked pipelines do not
construct one. Game asset compilation shares one context across its Passes and named variants.

Three distinct reuse mechanisms are implemented:

| Layer | Ownership and lifetime | Work avoided |
| --- | --- | --- |
| Checked module IR | ShaderCompiler, bounded to 16 snapshots in memory | Parsing and checking the matching primary Slang module |
| Target program cache | Development files under .vultra/shaders | Slang initialization, linking and SPIR-V emission on a valid hit |
| Driver pipeline cache | Device-owned VRI handle, current process only | Allows the driver to reuse graphics/compute pipeline compilation work |

The program cache uses checksummed .vshadercache files. A hash selects the file; the entire canonical request
must match, and every captured dependency is checked by content and include resolution. Invalid caches are logged
and recooked. Compiler failures preserve the last successful file. A cache hit returns owned bytecode and target
reflection before creating a Slang session. These development files include authoring request data and are not
shipping assets; package .vshaderc artifacts instead. Construct ShaderCompiler with an empty filesystem path
to disable disk caching while retaining scoped IR reuse. Do not put development caches under version control.

ShaderCompileStatistics reports program/module hits and frontend, link, code generation and total durations.
The compiler logs these durations and dependency invalidations. New source or specialization still requires
linking and SPIR-V emission; IR reuse does not make arbitrary edits free. Authored game-file changes conservatively
invalidate its dependent programs, including metadata edits. Stable frames neither compile nor create pipelines.
Native ShaderPipeline reload remains synchronous; game ShaderRuntime compiles candidates on its existing vtask
worker and publishes GPU state at a completed-frame boundary.

Pass Device::pipelineCache directly in VriGraphicsPipelineDesc or VriComputePipelineDesc when constructing native
pipelines. The built-in renderer, ShaderMaterial and drawing examples do this. Destroy pipelines before their
Device. The cache is neither persisted between launches nor supplied to ray-tracing pipelines, whose pinned VRI
descriptor does not expose it. It supplements the existing per-material pipeline-object reuse; changing a uniform
property still does not require shader compilation, pipeline creation or geometry upload.

## Cooking, caching and packaging

```powershell
xmake build vultra-shader
xmake run vultra-shader examples/shader/painted_metal.vshader --output build/material.vshaderc --include builtin/shaders --include external
xmake run vultra-shader experiment.slang --output build/experiment.vshaderc --entry compute:main --define SAMPLE_COUNT=4
```

Other options include repeated `--link`, `--capability`, `--variant`, `--profile`, `--ray-query` and `--reflection`.
The reflection JSON includes target offsets, resource categories, array strides and matrix layout information.
Game entries come from Pass declarations; `--entry` applies to the research path. Its stage names are vertex,
fragment, compute, geometry, hull, domain, task, mesh, raygen, intersection, anyhit, closesthit, miss and callable.

Both paths write a checksummed, bounded version-1 `.vshaderc` archive. Its source kind is explicitly `raw_slang` or
`game_shader`. Raw loading never interprets game properties, and game loading never interprets a raw program as
a material. `.vshader` now means text exclusively. There is no legacy binary format probe or compatibility reader.
Unreadable caches are explicitly recooked; invalid runtime artifacts fail. Failed compilation does not replace
the last successfully cooked file.

Cache identities include source text, ordered resolved search paths, link module contents, macros, capabilities,
target/profile, compiler/debug identity, language/parser/generator identity and generated template dependencies.
Each compilation snapshots the files Slang reads, then validates their content and search resolution before
returning. Whole game assets validate dependencies again after all passes/variants compile. Cache hits and changed
or re-resolved dependencies are logged.

VpkArchive::packProject cooks explicit `.vshader` and `.slang` assets, stores their `.vshaderc` replacements and
preserves their AssetIds in the rewritten manifest. Game shader instances and texture assets retain normal project
references. Packaged loads use artifacts directly without parsing, Java, authoring sources or runtime shader
compilation. Built-in renderer programs are supplied by the separate built-in pack. Relative linked/include
libraries used only during cooking need not be runtime assets.

For shader textures, project import preserves authored DDS mips and supported formats across 2D, array, 3D, cube
and cube-array resources. Other image formats use the existing 2D import/compression path. Color/linear/normal use
comes from the property. A scoped SceneShaderMaterials context owns imported texture views and shader runtimes;
applications may provide their own TextureResolver. The local VRI cube-array device-feature patch is documented
in [external/vri](../external/vri/README.md).

## Ownership and reload

ShaderAsset owns CPU metadata and per-pass/per-variant ShaderPrograms. MaterialInstance owns CPU values.
ShaderMaterial owns reflected uniform buffers, layouts, descriptor sets and cached VRI pipelines while borrowing
the asset, instance and explicitly supplied resource views. Destroy or detach a borrowing renderer before
destroying its materials. MaterialResource distinguishes shader instances from the existing numeric OpenPBR
category; accessing the wrong category reports an error.

ShaderRuntime owns one whole asset snapshot and its material instances. AddMaterial returns a stable index;
removeMaterial releases that instance after GPU completion, and a later addition may reuse its index. References
obtained from instance/material expire after successful asset publication. Rebind renderer slots after publication.

Call `poll` at a completed-frame boundary with a preparation callback. FileWatch debounces edits; an existing vtask
worker compiles an isolated candidate. The main thread reconciles every instance, creates all GPU candidates,
prepares current attachments/resources and publishes one complete snapshot. Compiler errors, missing resources,
contract errors and pipeline preparation failure retain the previous snapshot and expose diagnostics. Edits during
compilation/preparation discard stale candidates. The callback must bind current host resources, not descriptors
retained from an earlier RenderGraph.

Name/type-compatible overrides survive reload. New properties use defaults; removed or changed types are reset
with messages. Reload does not rewrite material files or upload geometry again. Normal texture encoding changes
also refresh reflected uniforms. Ordinary stable frames neither compile shaders nor create pipelines.

## Editor and language services

The material Inspector supports numbers, colors, enums, textures, scale/offset, variants and individual reset.
Hidden properties stay hidden. The research workbench prepares GPU material updates before command recording;
Inspector edits use the same MaterialResource and SceneShaderMaterials path as ExperimentSession and the packaged
player. Each consumer owns its context; GPU preparation runs before command recording. The player records its
scene graph, copies the result to the acquired backbuffer, then draws game/plugin UI.

The offline VS Code extension lives in `tools/vscode_vshader`. With the official Slang extension installed, launch
an extension development host without downloading a JavaScript toolchain:

```powershell
code --extensionDevelopmentPath=tools/vscode_vshader .
pwsh -File scripts/setup_vscode.ps1
```

On Unix use `scripts/setup_vscode.sh`. Setup merges local settings, preserves personal colors, adds game source
directories to both `slang.additionalSearchPaths` and `vultra.shader.includeDirectories`, and leaves the resulting
settings untracked. Run setup again after adding a shader directory, or add explicit include roots to both settings.
For another project/configuration, set `vultra.shader.compilerPath` to its built vultra-shader executable.

The extension highlights outer declarations and embedded Slang. It sends unsaved text to the compiler's projection
mode, writes ignored `.slang` documents and source maps below `build/generated/shader_editor`, and forwards native
completion, definition and hover requests. Diagnostics and editable ranges map back to `.vshader`; generated
property declarations support navigation but never accept generated completion edits. `Vultra: View Generated Slang`
opens the chosen pass's projection. Generated adapter `#line` directives are removed from language-service
projections while native code and physical line counts stay intact; actual compiler output preserves source locations.
FileWatch excludes generated build directories. Raw `.slang` continues using the official native extension directly.

## Parser maintenance and verification

The ANTLR generator and runtime are fixed at 4.13.2 with the project's MT/MTd runtime configuration. Generated C++
lexer/parser files are checked in and ANTLR types stay private. Normal builds do not require Java. After editing
the grammar, use `scripts/generate_shader_parser.ps1` or `.sh` with Java available; the scripts verify the generator
jar checksum and write only generated C++ files with normalized LF endings and whitespace. Program tokens are
native text chunks; raw
strings use their own lexer mode and delimiter, and continued preprocessor lines cannot end a block.

Shader tests cover native block boundaries, semantic rejection, typed variants, cooking and source-free loading;
GPU material tests cover bindings, texture defaults, sRGB upload, stable updates and transactional failure/recovery.
Inspector tests drive actual pointer events for edits, reset and variant selection. Target layout tests cover
padding, matrices, arrays, resource arrays and sparse sets. Surface tests compare indexed, meshlet, Forward and
deferred output and check alpha mask, dedicated DepthOnly, shadow, normal mapping, mirrors, backfaces, state
pipeline reuse and OpenPBR channels. Project tests delete source before loading their VPK and compare HDR output.
Native program tests retain all six ray tracing stages through discovery, explicit entry selection and a
source-free cooked roundtrip, while still rejecting combined stage bits. The triangle and Cornell examples
exercise actual raygen/miss/closest-hit pipelines; this does not establish game Surface participation in RT.
Windows player acceptance also cooks a game project, removes its source and compares identical external/embedded
VPK captures. Native language-service verification is available with:

```text
node tests/shader_language_service.js <vultra-shader executable> <official Slang slangd executable>
```

Further contracts, including game Surface participation in ray/path tracing, remain separate work. Hardware XR and
unvalidated graphics backends are not established by these offscreen Vulkan tests.

References: [Unity ShaderLab](https://docs.unity.com/en-us/engine/6000.5/manual/materials-and-shaders/shaders/reference/sl-reference),
[ANTLR lexer rules](https://github.com/antlr/antlr4/blob/master/doc/lexer-rules.md),
[Slang reflection](https://shader-slang.org/slang/user-guide/reflection.html),
[VS Code embedded languages](https://code.visualstudio.com/api/language-extensions/embedded-languages).
