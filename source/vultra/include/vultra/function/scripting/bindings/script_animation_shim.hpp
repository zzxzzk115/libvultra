#pragma once

// Shim declarations for the Lua `Animation` namespace + `Animator` usertype.
// Generated into the `animation` area. Bodies in script_animation_shim.cpp own
// the component access, UUID parsing, service dispatch, and struct conversion.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/world/components/animator_component.hpp"

#include <sol/sol.hpp>

#include <cstdint>
#include <string>

namespace vultra
{
    struct VBIND_MODULE(name = Animation, area = animation, service = animationService) AnimationModule
    {
    };
    struct VBIND_USERTYPE(name = Animator, handle = ScriptAnimatorRef, area = animation,
                          component = AnimatorComponent, accessor = animator) AnimatorUsertype
    {
    };

    // --- Animation namespace ---
    VBIND_FN(module = Animation, name = play, body = shim)
    bool animationPlay(ScriptContext& ctx, const ScriptEntity& entity, sol::optional<bool> restart);
    VBIND_FN(module = Animation, name = pause, body = shim)
    bool animationPause(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = Animation, name = stop, body = shim)
    bool animationStop(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = Animation, name = setAnimation, body = shim)
    bool animationSetAnimation(ScriptContext& ctx, const ScriptEntity& entity, const std::string& animationUuid,
                               sol::optional<bool> restart);
    VBIND_FN(module = Animation, name = setTime, body = shim)
    bool animationSetTime(ScriptContext& ctx, const ScriptEntity& entity, float seconds);
    VBIND_FN(module = Animation, name = setNormalizedTime, body = shim)
    bool animationSetNormalizedTime(ScriptContext& ctx, const ScriptEntity& entity, float normalizedTime);
    VBIND_FN(module = Animation, name = setSpeed, body = shim)
    bool animationSetSpeed(ScriptContext& ctx, const ScriptEntity& entity, float speed);
    VBIND_FN(module = Animation, name = setLoop, body = shim)
    bool animationSetLoop(ScriptContext& ctx, const ScriptEntity& entity, bool loop);
    VBIND_FN(module = Animation, name = state, body = shim)
    ScriptAnimatorPlaybackState animationState(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = Animation, name = jointCount, body = shim)
    std::uint32_t animationJointCount(ScriptContext& ctx, const std::string& skeletonUuid);
    VBIND_FN(module = Animation, name = duration, body = shim)
    float animationDuration(ScriptContext& ctx, const std::string& animationUuid);
    VBIND_FN(module = Animation, name = setFloat, body = shim)
    bool animationSetFloat(ScriptContext& ctx, const ScriptEntity& entity, const std::string& name, float value);
    VBIND_FN(module = Animation, name = setBool, body = shim)
    bool animationSetBool(ScriptContext& ctx, const ScriptEntity& entity, const std::string& name, bool value);
    VBIND_FN(module = Animation, name = setTrigger, body = shim)
    bool animationSetTrigger(ScriptContext& ctx, const ScriptEntity& entity, const std::string& name);
    VBIND_FN(module = Animation, name = getFloat, body = shim)
    float animationGetFloat(ScriptContext& ctx, const ScriptEntity& entity, const std::string& name);
    VBIND_FN(module = Animation, name = getBool, body = shim)
    bool animationGetBool(ScriptContext& ctx, const ScriptEntity& entity, const std::string& name);
    VBIND_FN(module = Animation, name = currentState, body = shim)
    sol::table animationCurrentState(ScriptContext& ctx, sol::this_state luaState, const ScriptEntity& entity);

    // --- Animator usertype properties ---
    VBIND_PROPERTY(usertype = Animator, name = skeleton, set = animatorSetSkeleton)
    std::string animatorGetSkeleton(ScriptContext& ctx, const ScriptAnimatorRef& self);
    void animatorSetSkeleton(ScriptContext& ctx, const ScriptAnimatorRef& self, const std::string& value);
    VBIND_PROPERTY(usertype = Animator, name = animation, set = animatorSetAnimation)
    std::string animatorGetAnimation(ScriptContext& ctx, const ScriptAnimatorRef& self);
    void animatorSetAnimation(ScriptContext& ctx, const ScriptAnimatorRef& self, const std::string& value);
    VBIND_PROPERTY(usertype = Animator, name = playing, set = animatorSetPlaying)
    bool animatorGetPlaying(ScriptContext& ctx, const ScriptAnimatorRef& self);
    void animatorSetPlaying(ScriptContext& ctx, const ScriptAnimatorRef& self, const bool& value);
    VBIND_PROPERTY(usertype = Animator, name = loop, set = animatorSetLoop)
    bool animatorGetLoop(ScriptContext& ctx, const ScriptAnimatorRef& self);
    void animatorSetLoop(ScriptContext& ctx, const ScriptAnimatorRef& self, const bool& value);
    VBIND_PROPERTY(usertype = Animator, name = speed, set = animatorSetSpeed)
    float animatorGetSpeed(ScriptContext& ctx, const ScriptAnimatorRef& self);
    void animatorSetSpeed(ScriptContext& ctx, const ScriptAnimatorRef& self, const float& value);
    VBIND_PROPERTY(usertype = Animator, name = time, set = animatorSetTime)
    float animatorGetTime(ScriptContext& ctx, const ScriptAnimatorRef& self);
    void animatorSetTime(ScriptContext& ctx, const ScriptAnimatorRef& self, const float& value);

    // --- Animator usertype methods ---
    VBIND_FN(usertype = Animator, name = play, body = shim)
    bool animatorPlay(ScriptContext& ctx, const ScriptAnimatorRef& self, sol::optional<bool> restart);
    VBIND_FN(usertype = Animator, name = pause, body = shim)
    bool animatorPause(ScriptContext& ctx, const ScriptAnimatorRef& self);
    VBIND_FN(usertype = Animator, name = stop, body = shim)
    bool animatorStop(ScriptContext& ctx, const ScriptAnimatorRef& self);
    VBIND_FN(usertype = Animator, name = setNormalizedTime, body = shim)
    bool animatorSetNormalizedTime(ScriptContext& ctx, const ScriptAnimatorRef& self, float normalizedTime);
    VBIND_FN(usertype = Animator, name = state, body = shim)
    ScriptAnimatorPlaybackState animatorState(ScriptContext& ctx, const ScriptAnimatorRef& self);
} // namespace vultra
