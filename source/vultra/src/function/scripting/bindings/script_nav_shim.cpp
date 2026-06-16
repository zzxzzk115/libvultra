#include "vultra/function/scripting/bindings/script_nav_shim.hpp"

#include "vultra/function/services/navigation_service.hpp"

#include <glm/vec3.hpp>

namespace vultra
{
    namespace
    {
        glm::vec3  toGlm(const ScriptVec3& v) { return {v.x, v.y, v.z}; }
        ScriptVec3 toScript(const glm::vec3& v) { return {v.x, v.y, v.z}; }
    } // namespace

    bool navBake(ScriptContext& ctx) { return ctx.navService ? ctx.navService->bake() : false; }

    bool navIsBaked(ScriptContext& ctx) { return ctx.navService ? ctx.navService->isBaked() : false; }

    sol::table navFindPath(ScriptContext& ctx, sol::this_state luaState, const ScriptVec3& start, const ScriptVec3& end)
    {
        sol::state_view lua(luaState);
        sol::table      result = lua.create_table();
        if (!ctx.navService)
            return result;
        const auto path = ctx.navService->findPath(toGlm(start), toGlm(end));
        int        index = 1;
        for (const auto& point : path)
            result[index++] = toScript(point);
        return result;
    }

    ScriptVec3 navNearestPoint(ScriptContext& ctx, const ScriptVec3& point)
    {
        return ctx.navService ? toScript(ctx.navService->nearestPoint(toGlm(point))) : point;
    }

    bool navSetAgentDestination(ScriptContext& ctx, const ScriptEntity& entity, const ScriptVec3& target)
    {
        return ctx.navService ? ctx.navService->setAgentDestination(entity.value, toGlm(target)) : false;
    }

    void navStopAgent(ScriptContext& ctx, const ScriptEntity& entity)
    {
        if (ctx.navService)
            ctx.navService->stopAgent(entity.value);
    }

    bool navAgentHasPath(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return ctx.navService ? ctx.navService->agentHasPath(entity.value) : false;
    }

    void navSetDebugDrawEnabled(ScriptContext& ctx, bool enabled)
    {
        if (ctx.navService)
            ctx.navService->setDebugDrawEnabled(enabled);
    }

    bool navDebugDrawEnabled(ScriptContext& ctx)
    {
        return ctx.navService ? ctx.navService->debugDrawEnabled() : false;
    }
} // namespace vultra
