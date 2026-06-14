#pragma once

// Shim declarations for the Lua `Scene` namespace. The IR pipeline
// (extract_bindings.py) reads these VBIND_FN annotations and generates the sol2
// registration (script_scene_binding.gen.cpp) + the LuaLS stub; the bodies in
// script_scene_shim.cpp own the irregular glue (world access, pointer-to-bool,
// entity wrapping). Each shim takes ScriptContext& first; the generated wrapper
// passes it plus the script-facing arguments.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"

#include <string>

namespace vultra
{
    // Module marker: declares the `Scene` namespace table; its functions come
    // from the shims below (module = Scene).
    struct VBIND_MODULE(name = Scene, service = sceneService) SceneModule
    {
    };

    VBIND_FN(module = Scene, name = load, body = shim)
    bool sceneLoad(ScriptContext& ctx, const std::string& uri);

    VBIND_FN(module = Scene, name = instantiate, body = shim)
    ScriptEntity sceneInstantiate(ScriptContext& ctx, const std::string& uri);

    VBIND_FN(module = Scene, name = instantiateChild, body = shim)
    ScriptEntity sceneInstantiateChild(ScriptContext& ctx, const std::string& uri,
                                       const ScriptEntity& parent, bool clearWorld);

    VBIND_FN(module = Scene, name = saveWorld, body = shim)
    bool sceneSaveWorld(ScriptContext& ctx, const std::string& uri);

    VBIND_FN(module = Scene, name = saveEntity, body = shim)
    bool sceneSaveEntity(ScriptContext& ctx, const std::string& uri, const ScriptEntity& root);
} // namespace vultra
