#include "vultra/function/scripting/bindings/script_animation_shim.hpp"

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/services/animation_service.hpp"
#include "vultra/function/world/world.hpp"

namespace vultra
{
    namespace
    {
        CoreUUID parseUuid(const std::string& value)
        {
            vbase::UUID uuid {};
            vbase::try_parse_uuid(value.c_str(), uuid);
            return CoreUUID(uuid);
        }

        AnimatorComponent* animatorComponent(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            return world ? world->registry().try_get<AnimatorComponent>(entity) : nullptr;
        }

        ScriptAnimatorPlaybackState toScript(const AnimatorPlaybackState& state)
        {
            return ScriptAnimatorPlaybackState {
                .valid          = state.valid,
                .playing        = state.playing,
                .loop           = state.loop,
                .speed          = state.speed,
                .time           = state.time,
                .duration       = state.duration,
                .normalizedTime = state.normalizedTime,
                .skeleton       = state.skeleton.valid() ? state.skeleton.toString() : std::string {},
                .animation      = state.animation.valid() ? state.animation.toString() : std::string {},
            };
        }
    } // namespace

    // --- Animation namespace ---
    bool animationPlay(ScriptContext& ctx, const ScriptEntity& entity, sol::optional<bool> restart)
    {
        return ctx.animationService ? ctx.animationService->play(entity.value, restart.value_or(false)) : false;
    }
    bool animationPause(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return ctx.animationService ? ctx.animationService->pause(entity.value) : false;
    }
    bool animationStop(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return ctx.animationService ? ctx.animationService->stop(entity.value) : false;
    }
    bool animationSetAnimation(ScriptContext& ctx, const ScriptEntity& entity, const std::string& animationUuid,
                               sol::optional<bool> restart)
    {
        return ctx.animationService ?
                   ctx.animationService->setAnimation(entity.value, parseUuid(animationUuid), restart.value_or(true)) :
                   false;
    }
    bool animationSetTime(ScriptContext& ctx, const ScriptEntity& entity, float seconds)
    {
        return ctx.animationService ? ctx.animationService->setTime(entity.value, seconds) : false;
    }
    bool animationSetNormalizedTime(ScriptContext& ctx, const ScriptEntity& entity, float normalizedTime)
    {
        return ctx.animationService ? ctx.animationService->setNormalizedTime(entity.value, normalizedTime) : false;
    }
    bool animationSetSpeed(ScriptContext& ctx, const ScriptEntity& entity, float speed)
    {
        return ctx.animationService ? ctx.animationService->setSpeed(entity.value, speed) : false;
    }
    bool animationSetLoop(ScriptContext& ctx, const ScriptEntity& entity, bool loop)
    {
        return ctx.animationService ? ctx.animationService->setLoop(entity.value, loop) : false;
    }
    ScriptAnimatorPlaybackState animationState(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return ctx.animationService ? toScript(ctx.animationService->playbackState(entity.value)) :
                                      ScriptAnimatorPlaybackState {};
    }
    std::uint32_t animationJointCount(ScriptContext& ctx, const std::string& skeletonUuid)
    {
        return ctx.animationService ? ctx.animationService->jointCount(parseUuid(skeletonUuid)) : 0u;
    }
    float animationDuration(ScriptContext& ctx, const std::string& animationUuid)
    {
        return ctx.animationService ? ctx.animationService->animationDuration(parseUuid(animationUuid)) : 0.0f;
    }
    bool animationSetFloat(ScriptContext& ctx, const ScriptEntity& entity, const std::string& name, float value)
    {
        return ctx.animationService ? ctx.animationService->setFloat(entity.value, name, value) : false;
    }
    bool animationSetBool(ScriptContext& ctx, const ScriptEntity& entity, const std::string& name, bool value)
    {
        return ctx.animationService ? ctx.animationService->setBool(entity.value, name, value) : false;
    }
    bool animationSetTrigger(ScriptContext& ctx, const ScriptEntity& entity, const std::string& name)
    {
        return ctx.animationService ? ctx.animationService->setTrigger(entity.value, name) : false;
    }
    float animationGetFloat(ScriptContext& ctx, const ScriptEntity& entity, const std::string& name)
    {
        return ctx.animationService ? ctx.animationService->getFloat(entity.value, name) : 0.0f;
    }
    bool animationGetBool(ScriptContext& ctx, const ScriptEntity& entity, const std::string& name)
    {
        return ctx.animationService ? ctx.animationService->getBool(entity.value, name) : false;
    }
    sol::table animationCurrentState(ScriptContext& ctx, sol::this_state luaState, const ScriptEntity& entity)
    {
        sol::state_view lua(luaState);
        auto            out = lua.create_table();
        if (!ctx.animationService)
            return out;
        const auto state          = ctx.animationService->controllerState(entity.value);
        out["valid"]              = state.valid;
        out["currentState"]       = state.currentState;
        out["nextState"]          = state.nextState;
        out["transitioning"]      = state.transitioning;
        out["transitionProgress"] = state.transitionProgress;
        out["normalizedTime"]     = state.normalizedTime;
        return out;
    }

    // --- Animator usertype ---
    std::string animatorGetSkeleton(ScriptContext& ctx, const ScriptAnimatorRef& self)
    {
        auto* animator = animatorComponent(ctx, self.entity);
        return animator && animator->skeleton.valid() ? animator->skeleton.toString() : std::string {};
    }
    void animatorSetSkeleton(ScriptContext& ctx, const ScriptAnimatorRef& self, const std::string& value)
    {
        if (auto* animator = animatorComponent(ctx, self.entity))
            animator->skeleton = parseUuid(value);
    }
    std::string animatorGetAnimation(ScriptContext& ctx, const ScriptAnimatorRef& self)
    {
        auto* animator = animatorComponent(ctx, self.entity);
        return animator && animator->animation.valid() ? animator->animation.toString() : std::string {};
    }
    void animatorSetAnimation(ScriptContext& ctx, const ScriptAnimatorRef& self, const std::string& value)
    {
        if (ctx.animationService)
            ctx.animationService->setAnimation(self.entity, parseUuid(value), true);
    }
    bool animatorGetPlaying(ScriptContext& ctx, const ScriptAnimatorRef& self)
    {
        auto* animator = animatorComponent(ctx, self.entity);
        return animator ? animator->playing : false;
    }
    void animatorSetPlaying(ScriptContext& ctx, const ScriptAnimatorRef& self, const bool& value)
    {
        if (!ctx.animationService)
            return;
        if (value)
            ctx.animationService->play(self.entity, false);
        else
            ctx.animationService->pause(self.entity);
    }
    bool animatorGetLoop(ScriptContext& ctx, const ScriptAnimatorRef& self)
    {
        auto* animator = animatorComponent(ctx, self.entity);
        return animator ? animator->loop : false;
    }
    void animatorSetLoop(ScriptContext& ctx, const ScriptAnimatorRef& self, const bool& value)
    {
        if (ctx.animationService)
            ctx.animationService->setLoop(self.entity, value);
    }
    float animatorGetSpeed(ScriptContext& ctx, const ScriptAnimatorRef& self)
    {
        auto* animator = animatorComponent(ctx, self.entity);
        return animator ? animator->speed : 1.0f;
    }
    void animatorSetSpeed(ScriptContext& ctx, const ScriptAnimatorRef& self, const float& value)
    {
        if (ctx.animationService)
            ctx.animationService->setSpeed(self.entity, value);
    }
    float animatorGetTime(ScriptContext& ctx, const ScriptAnimatorRef& self)
    {
        auto* animator = animatorComponent(ctx, self.entity);
        return animator ? animator->time : 0.0f;
    }
    void animatorSetTime(ScriptContext& ctx, const ScriptAnimatorRef& self, const float& value)
    {
        if (ctx.animationService)
            ctx.animationService->setTime(self.entity, value);
    }
    bool animatorPlay(ScriptContext& ctx, const ScriptAnimatorRef& self, sol::optional<bool> restart)
    {
        return ctx.animationService ? ctx.animationService->play(self.entity, restart.value_or(false)) : false;
    }
    bool animatorPause(ScriptContext& ctx, const ScriptAnimatorRef& self)
    {
        return ctx.animationService ? ctx.animationService->pause(self.entity) : false;
    }
    bool animatorStop(ScriptContext& ctx, const ScriptAnimatorRef& self)
    {
        return ctx.animationService ? ctx.animationService->stop(self.entity) : false;
    }
    bool animatorSetNormalizedTime(ScriptContext& ctx, const ScriptAnimatorRef& self, float normalizedTime)
    {
        return ctx.animationService ? ctx.animationService->setNormalizedTime(self.entity, normalizedTime) : false;
    }
    ScriptAnimatorPlaybackState animatorState(ScriptContext& ctx, const ScriptAnimatorRef& self)
    {
        return ctx.animationService ? toScript(ctx.animationService->playbackState(self.entity)) :
                                      ScriptAnimatorPlaybackState {};
    }
} // namespace vultra
