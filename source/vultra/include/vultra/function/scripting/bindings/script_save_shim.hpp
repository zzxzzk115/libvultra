#pragma once

// Shim declarations for the Lua `Save` namespace (typed KV store + named slots). Bodies in
// script_save_shim.cpp own the null-check and the generic set/get value marshalling.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

#include <string>

namespace vultra
{
    struct VBIND_MODULE(name = Save, area = save, service = saveService) SaveModule
    {
    };

    // set(key, value): value may be a number, boolean, or string.
    VBIND_FN(module = Save, name = set, body = shim) void saveSet(ScriptContext& ctx, const std::string& key, sol::object value);
    // get(key, default?): returns the stored value (typed) or `default` (or nil) when absent.
    VBIND_FN(module = Save, name = get, body = shim) sol::object saveGet(ScriptContext& ctx, sol::this_state luaState, const std::string& key, sol::object fallback);
    VBIND_FN(module = Save, name = has, body = shim) bool saveHas(ScriptContext& ctx, const std::string& key);
    VBIND_FN(module = Save, name = remove, body = shim) void saveRemove(ScriptContext& ctx, const std::string& key);
    VBIND_FN(module = Save, name = clear, body = shim) void saveClear(ScriptContext& ctx);

    VBIND_FN(module = Save, name = save, body = shim) bool saveSave(ScriptContext& ctx, const std::string& slot);
    VBIND_FN(module = Save, name = load, body = shim) bool saveLoad(ScriptContext& ctx, const std::string& slot);
    VBIND_FN(module = Save, name = hasSlot, body = shim) bool saveHasSlot(ScriptContext& ctx, const std::string& slot);
    VBIND_FN(module = Save, name = deleteSlot, body = shim) bool saveDeleteSlot(ScriptContext& ctx, const std::string& slot);
    VBIND_FN(module = Save, name = listSlots, body = shim) sol::table saveListSlots(ScriptContext& ctx, sol::this_state luaState);
} // namespace vultra
