#pragma once

// Shim declarations for the Lua `Nav` namespace (Recast/Detour navigation). Bodies in
// script_nav_shim.cpp own the service null-check and table building.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"

#include <sol/sol.hpp>

namespace vultra
{
    struct VBIND_MODULE(name = Nav, area = navigation, service = navService) NavModule
    {
    };

    VBIND_FN(module = Nav, name = bake, body = shim) bool navBake(ScriptContext& ctx);
    VBIND_FN(module = Nav, name = isBaked, body = shim) bool navIsBaked(ScriptContext& ctx);
    VBIND_FN(module = Nav, name = findPath, body = shim) sol::table navFindPath(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& start, const ScriptVec3& end);
    VBIND_FN(module = Nav, name = nearestPoint, body = shim) ScriptVec3 navNearestPoint(ScriptContext& ctx, const ScriptVec3& point);
    VBIND_FN(module = Nav, name = setAgentDestination, body = shim) bool navSetAgentDestination(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& target);
    VBIND_FN(module = Nav, name = stopAgent, body = shim) void navStopAgent(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = Nav, name = agentHasPath, body = shim) bool navAgentHasPath(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = Nav, name = setDebugDrawEnabled, body = shim) void navSetDebugDrawEnabled(ScriptContext& ctx, bool enabled);
    VBIND_FN(module = Nav, name = debugDrawEnabled, body = shim) bool navDebugDrawEnabled(ScriptContext& ctx);
} // namespace vultra
