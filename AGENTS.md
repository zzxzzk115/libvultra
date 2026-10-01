# Vultra AI Development Rules

AI-assisted development is allowed in this repository. These rules apply throughout the repository. Every change must remain understandable, verifiable and editable by the maintainer without further AI assistance. Keeping Vultra a small, readable research framework is the primary goal.

An explicit user request may change a corresponding constraint. An agent must not weaken this file, formatting rules or checks to accommodate generated code. Complete necessary work already authorized by the task without repeatedly asking about routine implementation choices. Explain and obtain explicit direction before materially expanding the task's scope.

## 1. Scope of Changes

- Solve the current task only. Do not perform incidental repository-wide refactoring, bulk renaming, directory moves, unrelated example rewrites or toolchain upgrades. Preserve unrelated user changes.
- Do not add interfaces, configuration switches, compatibility layers, fallback implementations, empty modules or TODO scaffolding for hypothetical future needs. Do not remove existing functionality under the label of simplification.
- Use one clear implementation path. Do not retain old and new implementations together unless the current migration requires it; state what remains before the old path can be removed.
- Before the first public release, keep every serialized-data and file-format version at `1`. Make format changes as deliberate breaking changes: update writers, readers, examples and tests together; do not add migration paths, compatibility readers or version bumps.
- Do not commit, push, publish, reset or clean the workspace without authorization. Do not make an AI runtime, knowledge base, delegation protocol or maintenance harness a prerequisite for working on this project.

## 2. Architecture Boundaries

- Follow the `source/{core,platform,drivers,assets,servers,scene,ui,main,api}` hierarchy, with `scripting` as an optional implemented module. Each module owns `include/vultra/<module>` and `src`; create future runtime or editor directories only when implemented.
- VRI is the rendering abstraction boundary. Use its descriptors and commands directly; do not add another general RHI, resource object hierarchy or backend dispatch layer above it. Keep native interop within the existing platform or XR boundaries.
- Keep one public `vultra` static library. Basic drawing examples must remain able to use VRI and RenderGraph directly, without constructing a scene tree, importing assets or using the built-in renderer.
- Keep the explicit, code-driven RenderGraph and the checked-in generated API from annotated C++ declarations. Do not add an external fg library, a second graph executor, a global plugin registry or a separate binding pipeline.
- Follow the BaseApp / DesktopApp / ImGuiApp lifecycle. Keep logic updates, UI construction, command recording, submission and presentation in their respective phases. Do not construct UI in `onUpdate()`.

## 3. C++ and Ownership

- Use the project's existing C++23, standard library and RAII conventions. Prefer direct members, value semantics and clear single ownership. `std::unique_ptr` is appropriate for optional or deferred construction. Do not use `shared_ptr`, global services or singletons to avoid designing lifetimes.
- A small function should perform a meaningful, nameable operation. Do not add forwarding layers that neither remove duplication nor express a constraint. Do not create managers, factories, interfaces or layered base classes for a single caller.
- Existing App virtual functions and RenderGraph callbacks are intentional extension points. Prefer composition elsewhere. Do not replace direct code with complex templates, macro generation or implicit registration.
- Make data flow, ownership and destruction order visible in the code. Avoid copying large objects; use references or `std::span` for contiguous read-only data. Never retain views or callback captures that refer to expired temporary objects.
- Report initialization and external-input failures through the existing error handling and spdlog, identifying the operation and cause. Do not swallow exceptions, fabricate success, silently skip necessary passes or hardcode results to conceal failures.
- Check real boundaries and invariants owned by the current layer. Preserve RenderGraph's correctness checks. Do not duplicate Vulkan validation or add speculative defensive branches throughout normal execution.

## 4. Shaders, GPU Work and Performance

- Keep shaders grouped under `builtin/shaders/lib`, `resources` and `passes`. Related pipeline stages may share a source file; do not combine unrelated algorithms behind extensive conditional compilation. Use the configured include roots instead of traversing upward into `external`.
- When changing rendering code, check CPU/shader layouts, descriptor bindings, resource states, synchronization, color spaces and destruction timing. Comments should explain non-obvious constraints rather than restate the code.
- CPU object destruction does not imply GPU completion. Establish a completion point for the final use of resources, views, cached GUI textures and XR images. Preserve the existing single-frame-in-flight policy unless the task explicitly changes it.
- Review new per-frame allocations, large copies, shader compilation, GPU resource recreation and waits. Reuse stable resources and graphs. Do not introduce schedulers, threading systems or complex caches for unmeasured performance assumptions.
- Do not add silent emulation or fallback paths to expand hardware support. New requirements must fit the supported baseline. Preserve existing documented format selection and runtime handling.

## 5. Formatting and Readability

- Follow `.clang-format` and `.clang-tidy`: `lower_snake_case` filenames, PascalCase types, camelCase functions/variables, `m_Name` private members and `eName` enumerators. Use four spaces, a 120-column limit and braces on new lines.
- Expand functions, lambdas, branches and loops as required by the formatter. Do not pack multiple statements onto one line, declare multiple variables in one declaration or use nested ternary expressions. Do not reformat unrelated files or third-party code for a local change.
- Separate C++ include groups with blank lines: relative project headers, `<vultra/...>`, third-party headers, then standard/C/system headers. Explain any local exception required by a dependency's include order.
- Names should explain responsibilities and comments should explain reasons. Remove obsolete descriptions. Do not add redundant comments or use lengthy explanations to compensate for unreadable implementations.

## 6. Dependencies and Local State

- Preserve the xmake-template build approach and centralized dependency declarations. Reuse existing libraries first. Add dependencies only for a concrete current need; do not reimplement available library facilities for small features or import large libraries for possible future use.
- Do not incidentally change dependency versions, the MSVC runtime configuration, build-option defaults or supported platforms. Keep third-party changes minimal and record their public source, fixed revision, license and local patches.
- Cite verifiable public upstream sources for public implementations. Do not use inaccessible private repository paths as public provenance. Preserve actual attribution and asset licenses; never remove notices to obscure origins.
- Keep personal VS Code colors, generated compilation databases, capture outputs and ImGui layouts out of source control. Use the existing editor setup scripts. Isolate layouts by stable AppName; do not restore a shared run-directory `imgui.ini`.

## 7. Workflow and Verification

- Read relevant implementations, callers and documented contracts, and inspect workspace changes before editing. Do not guess APIs from their names. Platform scope, build commands and limitations are documented in the [README](README.md) and [development guide](docs/guide.md).
- Update affected callers, examples and necessary documentation when changing an API. Do not disable examples, weaken assertions, relax image-error thresholds or suppress diagnostics to make checks pass.
- Build affected targets and run formatting and clang-tidy checks on modified project code. Tests should establish observable behavior or a real regression, not mechanically duplicate the implementation. Do not launch GPU tests for documentation-only changes.
- Rendering changes require finite-frame runs, GPU diagnostic checks and relevant image readback. Persistence and hot-reload changes need failure/recovery coverage. Compilation or a successful exit code alone does not prove correct rendering.
- Once appropriate checks pass, do not expand or repeat them without a reason. Put temporary verification outputs in ignored `build/.tmp/`; never overwrite user layouts, experiment data or existing captures.
- Completion reports must distinguish the changes, what was actually verified and what remains unverified. Report missing headsets, runtimes or other equipment accurately. Do not describe offscreen tests as hardware validation or claim complete ports or standards conformance without evidence.

## 8. Documentation

- Write all repository documentation in English, including README files, guides and agent instructions. Preserve upstream license texts and attribution.
- Keep human-facing usage, API contracts and supported limitations in `docs/`, outside `docs/ai/`. Put AI handoffs, continuation context and work-state notes in `docs/ai/`; do not mix them into user guides.
- Migration status and outstanding work are recorded in [docs/ai/handoff.md](docs/ai/handoff.md). Treat that document as a dated snapshot, verify relevant claims against current code and update it when migration work changes the recorded state. Pending items do not authorize unrelated implementation work.
- The README describes available behavior and limitations; source comments describe local constraints. Keep stable development rules here rather than task logs, machine-specific absolute paths or temporary troubleshooting notes.
- Keep patches and explanations easy to review. If the control flow still requires AI to explain it, simplify the implementation first.

Reference: the principles of simple implementations, explicit lifetimes and limited abstraction in [NoGraphicsAPI's AGENTS.md](https://github.com/sebbbi/NoGraphicsAPI/blob/main/AGENTS.md). These rules are written for Vultra's research use and existing design, retaining the standard library, exceptions, RAII, the App inheritance hierarchy and the 120-column format.
