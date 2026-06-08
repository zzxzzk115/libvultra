# Runtime size reduction via the export-template model

Status: **Phase 1 implemented & validated** (self-contained binaries embed a zstd `builtin.vpk`).
**Phase 2 (fully-thin runtime via export injection) is pending** — and is also the only path for
wasm/android, where `.rc`/`.S` embedding does not apply.

## Implemented (Phase 1)

A zstd `builtin.vpk` is produced at build time (`tools/builtinpack` + the `vultra.builtin_pack`
rule) and embedded into every self-contained binary (editor, examples, **and — interim — the
desktop runtime**) via `.rc` (Windows) / `.S` `.incbin` (Linux/macOS). A static-init mount opens
it from memory (`vasset::VpkFileSystem(blob)`) and installs it as the `builtin::` source, before
`ShaderSystem`/`ImGuiSystem` init. Shaders, fonts, and LTC textures read from it; their C-array
headers were stripped from `vultra` (fonts fully retired: `font_task` removed, headers `git rm`'d).

Measured (raw exe, Windows): editor **50.3 → 45.6 MB**, gltf example **40.0 → 35.4 MB**, runtime
**40.2 → 35.5 MB**. `vultra.lib` shed ~9.7 MB of arrays; self-contained binaries net ~−4.6 MB
(they trade raw arrays for the ~5 MB *compressed* embedded pack and stay single-file). All run
clean pure-pack (no fallback). Cursors + render-graph JSON still use their (small) headers.

## Phase 2 (pending) — the fully-thin runtime

The remaining ~5 MB (and the wasm/android requirement) needs: a `vasset` vpk append/merge; export
injecting builtin resources from the editor's embedded pack into the project VPK (under a
`builtin/...` prefix); and the runtime mounting that VPK early (in `runtime_main.cpp::onConfigureDemo`,
before `ShaderSystem`) as the `builtin::` source via a path-prefix adapter. Then the desktop runtime
drops the `vultra.builtin_pack` rule and shrinks to ~30 MB; wasm/android work for the first time.

---


## Context

The runtime executable is ~40 MB (raw, measured via the PE section table):

| Section | Size | Notes |
|---|---|---|
| `.text` | 22.8 MB | code (Vulkan, sol2, entt, Jolt, etc.) |
| `.rdata` | 15.9 MB | const data: **~10 MB is embedded builtin resources**, rest is RTTI/vtables/string tables |
| other | ~2 MB | `.data`, `.pdata`, reloc |

Embedded builtin payload in the runtime (the real bytes, not the 53 MB of header *text*):

| Resource | In binary | Form |
|---|---|---|
| Noto CJK font | ~4.8 MB | lz4 C-array (`noto_sans_cjk_otf_lz4`) |
| Color emoji + MDI icons | ~1.9 MB | lz4 C-array |
| Shaders (highend+compat+web) | ~2.9 MB | **raw** SPIR-V/VSHLIB C-array |
| LTC + cursor textures | ~0.8 MB | raw |
| render graphs / i18n | ~0.4 MB | raw / lz4 |

The goal is a smaller **raw runtime exe**. The decisive move is not compression (fonts are already compressed; an installer would compress the raw shaders anyway) but **removing resources from the runtime binary entirely**.

## Decision: thin engine library + an opt-in embedded builtin pack

The key realization is that there are **three** kinds of binaries, not two:

1. **Editor** (`vultra-app`) — the authoring tool and source pool for export. Self-contained.
2. **Examples / standalone demos** (`example-sponza`, `example-gaussian-splatting`, ... ~12 targets) — use the engine directly, are neither editor nor export template, but still need builtin shaders to render. Self-contained.
3. **Export-template runtime** (`vultra-runtime`) — ships *with no resources* and always runs paired with a project VPK.

So the split is **self-contained binaries vs the export template**, not "editor vs runtime":

- **`vultra` (the engine library)** carries **zero** embedded resources and reads every builtin resource through the **VFS** under stable logical paths. This is what makes the engine reusable for all three.
- **`vultra_builtin_pack`** — a small, opt-in component (object/static lib + xmake rule) that contains the embedded `builtin.vpk` blob and a startup hook that mounts it **from memory**. Any binary that wants builtin resources self-contained simply links it. **Editor and all examples opt in.**
- **Export-template runtime opts out**: it does *not* link `vultra_builtin_pack`. Instead it mounts the project VPK first, and **export injects** the builtin resources the game needs (shaders always; default font; referenced textures) into that VPK.

This answers "examples need builtin shaders too": they get them exactly like the editor — by linking `vultra_builtin_pack` (the embedded `builtin.vpk`, memory-mounted). Only the export template is thin, because only it is guaranteed to ship alongside a project VPK.

Expected outcome: **`vultra-runtime` ~40 → ~30 MB** (no embedded resources). Editor and examples stay self-contained (each embeds `builtin.vpk` ~8 MB instead of ~10 MB of C-array headers — marginally smaller, and a much faster build: no 25 MB headers to recompile). Exported game = thin runtime (~30 MB) + project VPK (builtin shaders ~1 MB zstd + default font + scene assets).

## Unified loading: one path, via the VFS

To avoid `#ifdef`-ing every consumer between "embedded symbol" and "VFS read", **all builtin resource reads go through the VFS** under stable logical paths (e.g. `builtin://shaders/highend.vshlib`, `builtin://fonts/<name>`). The only difference between binaries is *what is mounted*, decided purely by whether `vultra_builtin_pack` is linked:

- **Self-contained binaries (editor, examples)** link `vultra_builtin_pack`, which mounts the embedded `builtin.vpk` from memory via `vasset::VpkFileSystem(std::vector<std::byte>)` + `openVpkFromMemory` (already added in `external/vasset`). One embedded pack replaces the per-resource C-array headers.
- **Export-template runtime** mounts the **project VPK** (on disk, beside the exe) via the existing path-based `VpkFileSystem`; export already injected the builtin resources under the same logical paths.

Net: `shader_system.cpp`, `imgui_system.cpp`, the texture loaders, and the render-graph registry all read from the VFS, oblivious to whether the bytes came from an embedded pack (editor/examples) or a disk pack (runtime). No per-consumer branching; the choice is one link-time decision.

## Per-resource plan

(Consumption sites from the resource map; all in `source/vultra` unless noted.)

| Resource | Today | Plan |
|---|---|---|
| **Shaders** (`shader_system.cpp` `loadFromMemory`, `builtin_shaders_highend_vshlib` etc.; also `debug_draw_interface.cpp`) | embedded raw C-array | read `.vshlib` bytes from VFS; export injects the 3 libs from `builtin/shader_lib/*.vshlib` |
| **Fonts** (`imgui_system.cpp` `addCompressedFontTTF`) | embedded lz4 C-array (base+CJK+emoji+MDI) | editor: from `builtin.vpk`; runtime: default font (+locale CJK) injected by export into the project VPK for the in-game ImGui atlas |
| **Textures** LTC (`deferred_lighting_pass.cpp`) + cursors (`vulkan_imgui.cpp`, `camera_system.cpp`) | embedded raw | read from VFS (lazy); export injects |
| **Render graphs** (`builtin_rendergraph_registry.cpp`, served by `asset_system.cpp`) | embedded raw JSON | already VFS-served; move bytes into the pack |
| **i18n** (`editor_i18n.cpp`) | embedded lz4, **editor-only** | unchanged; runtime never needs engine i18n (a game ships its own strings) |

## Build-time pack production

`builtin.vpk` is produced at build time by the `builtinpack` host tool (already written, `tools/builtinpack/main.cpp`): a `vasset`-only exe that packs `(logicalPath, sourceFile)` pairs into a zstd VPK. xmake builds the tool, then an `on_config`/build step runs it over `builtin/shader_lib/*.vshlib`, the chosen font(s), `builtin/textures/...`, and `builtin/render/*.vrg.json`. The editor embeds the result; the existing C-array `*_task`s (shader/font/texture/i18n) are retired for the runtime path.

## Bootstrap ordering

Subsystem init order today: `ShaderSystem` (9) and `ImGuiSystem` (12) run **before** `AssetSystem` (14), which is where the VFS mounts. Under the thin-runtime model the builtin source must be mounted earlier:

- Add an **early mount** step (a small pre-`ShaderSystem` bootstrap) that mounts the builtin source: the embedded `builtin.vpk` (self-contained binaries, via `vultra_builtin_pack`'s startup hook) or the project VPK (export-template runtime). `AssetSystem` later mounts/uses the same VFS for project assets.
- This removes the current ordering hazard by construction (vpk-first boot, no embedded fallback to race).

## The one part that is a *new feature*, not plumbing

"根据引用关系决定打包哪些字体" (pack only *referenced* fonts) presupposes a font-asset system that does not exist yet: UI text components carry only `fontSizePx` ([ui_components.hpp:53](../source/vultra/include/vultra/function/world/components/ui_components.hpp#L53)); in-game text renders from the shared ImGui atlas, with no font assets and no scene→font reference edges.

True reference-driven font packing therefore requires (separate, larger, multi-subsystem):

1. a `FontAsset` type + importer;
2. UI text component → font-asset reference (uri/uuid) alongside `fontSizePx`;
3. in-game text rendering using the referenced font (dynamic ImGui font registration or a dedicated text path);
4. reference tracking so the scene→asset reachability graph includes fonts;
5. export packing only the reachable fonts (builtin default + project fonts).

Until then, **export packs a default font set unconditionally** (base + the project's locale CJK if needed). The default base font should be a freely redistributable one — **Roboto (Apache-2.0)** or **Liberation Sans** (Arial-metric, OFL). Arial itself is proprietary and must not be bundled.

## Phasing

- **Phase 1 — unify loading via VFS + `vultra_builtin_pack`.** Produce `builtin.vpk`; create the `vultra_builtin_pack` opt-in component (embedded blob + memory-mount hook); link it into the editor **and all examples**; strip the C-array resource headers from the `vultra` library and reroute its resource loads (shaders → textures → render graphs → fonts) to the VFS. All current binaries still self-contained; build gets faster, behavior unchanged. *Verify editor, an example, and play-in-editor still render.*
- **Phase 2 — thin runtime + export injection.** The export-template runtime does *not* link `vultra_builtin_pack`; export injects builtin resources (shaders, default font, textures, render graphs) into the project VPK; runtime mounts the project VPK first and boots from it. *Measure `vultra-runtime` exe (~30 MB target).*
- **Phase 3 (optional) — font-asset system.** The new feature above, enabling per-reference font packing.

## Open decisions (resolve before/early in Phase 1)

1. **Scheme/paths** for builtin resources in the VFS (`builtin://…` vs folding into `res://__builtin__/…`).
2. **Where the early mount lives** (a dedicated pre-`ShaderSystem` bootstrap vs hoisting part of `AssetSystem::configure`).
3. **Default font** choice: Roboto vs Liberation Sans.
4. **CJK policy** for export: include the CJK font when the project's locale/strings need it (heuristic) vs an explicit project setting.
5. **Editor delivery**: embedded `builtin.vpk` (recommended, uses the memory-open) vs keeping the C-array headers for the editor only.

## Already landed

- `external/vasset`: `openVpkFromMemory` / `readVpkFileFromMemory` + a memory-backed `VpkFileSystem` ctor (shared `parseVpk(readAt)` refactor). Built clean. Enables the editor to mount an embedded `builtin.vpk` from memory.
- `tools/builtinpack/main.cpp`: the build-time pack tool (not yet wired into xmake).
