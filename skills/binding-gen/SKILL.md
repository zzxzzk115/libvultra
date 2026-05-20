---
name: binding-gen
description: Use when generating, updating, replacing, or reviewing concrete libvultra Lua binding modules; creating script_<subsystem>_binding.hpp/.cpp files; wiring new bindings into registerScriptBindings; extending ScriptContext for service-backed bindings; preserving Lua API compatibility while exposing engine subsystems, components, assets, input, rendering controls, SRP-safe facades, or shader/material scripting surfaces.
---

# Binding Gen

## Binding Generation Workflow

Inspect before generating. Read the target subsystem header/source, nearby service or component interfaces, existing binding modules in `source/vultra/include/vultra/function/scripting/bindings/` and `source/vultra/src/function/scripting/bindings/`, `script_binding.cpp`, `ScriptContext`, and `script_types.hpp`.

Create one focused module per subsystem. Use `script_<subsystem>_binding.hpp` for the declaration and `script_<subsystem>_binding.cpp` for implementation. Name the entry point `registerScript<Subsystem>Bindings(sol::state& lua, ScriptContext& ctx)` when context is needed, or omit `ctx` only for pure value/math bindings.

Wire the module by including its header in `source/vultra/src/function/scripting/script_binding.cpp` and calling its register function from `registerScriptBindings` after its dependencies. Register math/value types before APIs that consume them.

Prefer narrow script-facing wrappers over broad automatic reflection. Bind only operations that are stable, safe, and useful from Lua. Avoid dumping entire C++ classes when setters, getters, and service commands can preserve invariants.

## API Shape Rules

Use existing Lua naming style from nearby bindings. Keep names short, predictable, and behavior-oriented. Preserve existing Lua API names unless the user explicitly asks for a migration.

Validate entity and service availability before use. Use `ScriptContext::isValid` for entity-backed APIs, check service pointers for service-backed APIs, and return `nil`, `false`, or simple error values for recoverable absence.

Do not expose raw lifetime-sensitive pointers or references to RHI objects, framegraph objects, renderers, asset-cache records, GPU resource internals, or transient command buffers. Use IDs, URIs, handles, or queued commands owned by C++ instead.

Keep ownership in C++. Lua may reference entities, asset URIs, small value types, and stable script handles, but C++ must own engine services, GPU resources, and framegraph lifetime.

Avoid exceptions crossing Lua boundaries. If existing local bindings throw for programmer errors, keep behavior consistent, but prefer protected calls, explicit validation, and clear logging for new APIs.

## Context And Service Expansion

Extend `ScriptContext` only when the binding genuinely needs an engine service. Add a forward declaration in `script_context.hpp`, add a pointer field, initialize it in `ScriptSystem::onInit`, and use `tryGet` unless the service is required for scripting startup.

Bind through public service interfaces whenever possible. If the desired operation exists only in a concrete system, first consider whether a small service method is the better boundary.

Keep editor/runtime behavior deterministic. Do not do direct filesystem watching, shader compilation, GPU uploads, or render-thread work from arbitrary Lua callbacks unless a service already queues that work safely.

## Generated Module Checklist

Add the header and source in the existing scripting binding directories.

Include only the headers needed by the binding. Prefer subsystem public headers and service interfaces over private implementation headers.

Register usertypes, enums, and functions in a stable order. Keep overloads explicit and avoid ambiguous lambdas that make Lua errors hard to read.

Add or update minimal runtime/editor examples only when the repo already has a place for script examples. Do not invent a new examples structure unless requested.

Build the affected target after C++ changes. For documentation-only changes, validate the skill shape and sanity-check references with `rg`.
