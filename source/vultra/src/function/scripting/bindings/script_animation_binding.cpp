#include "vultra/function/scripting/bindings/script_animation_binding.hpp"

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/animation_service.hpp"
#include "vultra/function/world/components/animator_component.hpp"
#include "vultra/function/world/world.hpp"

#include <sol/sol.hpp>

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

    void registerScriptAnimationBindings(sol::state& lua, ScriptContext& ctx)
    {
        lua.new_usertype<ScriptAnimatorPlaybackState>("AnimatorPlaybackState",
                                                      "valid",
                                                      &ScriptAnimatorPlaybackState::valid,
                                                      "playing",
                                                      &ScriptAnimatorPlaybackState::playing,
                                                      "loop",
                                                      &ScriptAnimatorPlaybackState::loop,
                                                      "speed",
                                                      &ScriptAnimatorPlaybackState::speed,
                                                      "time",
                                                      &ScriptAnimatorPlaybackState::time,
                                                      "duration",
                                                      &ScriptAnimatorPlaybackState::duration,
                                                      "normalizedTime",
                                                      &ScriptAnimatorPlaybackState::normalizedTime,
                                                      "skeleton",
                                                      &ScriptAnimatorPlaybackState::skeleton,
                                                      "animation",
                                                      &ScriptAnimatorPlaybackState::animation);

        lua.new_usertype<ScriptAnimatorRef>(
            "Animator",
            "valid",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptAnimatorRef& self) {
                return animatorComponent(ctx, self.entity) != nullptr;
            }),
            "skeleton",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptAnimatorRef& self) {
                    auto* animator = animatorComponent(ctx, self.entity);
                    return animator && animator->skeleton.valid() ? animator->skeleton.toString() : std::string {};
                },
                [&ctx](const ScriptAnimatorRef& self, const std::string& value) {
                    if (auto* animator = animatorComponent(ctx, self.entity))
                        animator->skeleton = parseUuid(value);
                }),
            "animation",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptAnimatorRef& self) {
                    auto* animator = animatorComponent(ctx, self.entity);
                    return animator && animator->animation.valid() ? animator->animation.toString() : std::string {};
                },
                [&ctx](const ScriptAnimatorRef& self, const std::string& value) {
                    if (!ctx.animationService)
                        return;
                    ctx.animationService->setAnimation(self.entity, parseUuid(value), true);
                }),
            "playing",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptAnimatorRef& self) {
                    auto* animator = animatorComponent(ctx, self.entity);
                    return animator ? animator->playing : false;
                },
                [&ctx](const ScriptAnimatorRef& self, bool value) {
                    if (!ctx.animationService)
                        return;
                    if (value)
                        ctx.animationService->play(self.entity, false);
                    else
                        ctx.animationService->pause(self.entity);
                }),
            "loop",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptAnimatorRef& self) {
                    auto* animator = animatorComponent(ctx, self.entity);
                    return animator ? animator->loop : false;
                },
                [&ctx](const ScriptAnimatorRef& self, bool value) {
                    if (ctx.animationService)
                        ctx.animationService->setLoop(self.entity, value);
                }),
            "speed",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptAnimatorRef& self) {
                    auto* animator = animatorComponent(ctx, self.entity);
                    return animator ? animator->speed : 1.0f;
                },
                [&ctx](const ScriptAnimatorRef& self, float value) {
                    if (ctx.animationService)
                        ctx.animationService->setSpeed(self.entity, value);
                }),
            "time",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptAnimatorRef& self) {
                    auto* animator = animatorComponent(ctx, self.entity);
                    return animator ? animator->time : 0.0f;
                },
                [&ctx](const ScriptAnimatorRef& self, float value) {
                    if (ctx.animationService)
                        ctx.animationService->setTime(self.entity, value);
                }),
            "play",
            [&ctx](const ScriptAnimatorRef& self, sol::optional<bool> restart) {
                return ctx.animationService ? ctx.animationService->play(self.entity, restart.value_or(false)) : false;
            },
            "pause",
            [&ctx](const ScriptAnimatorRef& self) {
                return ctx.animationService ? ctx.animationService->pause(self.entity) : false;
            },
            "stop",
            [&ctx](const ScriptAnimatorRef& self) {
                return ctx.animationService ? ctx.animationService->stop(self.entity) : false;
            },
            "setNormalizedTime",
            [&ctx](const ScriptAnimatorRef& self, float normalizedTime) {
                return ctx.animationService ? ctx.animationService->setNormalizedTime(self.entity, normalizedTime) :
                                              false;
            },
            "state",
            [&ctx](const ScriptAnimatorRef& self) {
                return ctx.animationService ? toScript(ctx.animationService->playbackState(self.entity)) :
                                              ScriptAnimatorPlaybackState {};
            });

        auto animation = script_binding::getOrCreateTable(lua, "Animation");
        animation.set_function("play", [&ctx](const ScriptEntity& entity, sol::optional<bool> restart) {
            return ctx.animationService ? ctx.animationService->play(entity.value, restart.value_or(false)) : false;
        });
        animation.set_function("pause", [&ctx](const ScriptEntity& entity) {
            return ctx.animationService ? ctx.animationService->pause(entity.value) : false;
        });
        animation.set_function("stop", [&ctx](const ScriptEntity& entity) {
            return ctx.animationService ? ctx.animationService->stop(entity.value) : false;
        });
        animation.set_function("setAnimation", [&ctx](const ScriptEntity& entity,
                                                      const std::string& animationUuid,
                                                      sol::optional<bool> restart) {
            return ctx.animationService ?
                       ctx.animationService->setAnimation(entity.value, parseUuid(animationUuid), restart.value_or(true)) :
                       false;
        });
        animation.set_function("setTime", [&ctx](const ScriptEntity& entity, float seconds) {
            return ctx.animationService ? ctx.animationService->setTime(entity.value, seconds) : false;
        });
        animation.set_function("setNormalizedTime", [&ctx](const ScriptEntity& entity, float normalizedTime) {
            return ctx.animationService ? ctx.animationService->setNormalizedTime(entity.value, normalizedTime) : false;
        });
        animation.set_function("setSpeed", [&ctx](const ScriptEntity& entity, float speed) {
            return ctx.animationService ? ctx.animationService->setSpeed(entity.value, speed) : false;
        });
        animation.set_function("setLoop", [&ctx](const ScriptEntity& entity, bool loop) {
            return ctx.animationService ? ctx.animationService->setLoop(entity.value, loop) : false;
        });
        animation.set_function("state", [&ctx](const ScriptEntity& entity) {
            return ctx.animationService ? toScript(ctx.animationService->playbackState(entity.value)) :
                                          ScriptAnimatorPlaybackState {};
        });
        animation.set_function("jointCount", [&ctx](const std::string& skeletonUuid) {
            return ctx.animationService ? ctx.animationService->jointCount(parseUuid(skeletonUuid)) : 0u;
        });
        animation.set_function("duration", [&ctx](const std::string& animationUuid) {
            return ctx.animationService ? ctx.animationService->animationDuration(parseUuid(animationUuid)) : 0.0f;
        });

        // --- Animator graph (state machine) parameters & introspection ---
        animation.set_function("setFloat", [&ctx](const ScriptEntity& entity, const std::string& name, float value) {
            return ctx.animationService ? ctx.animationService->setFloat(entity.value, name, value) : false;
        });
        animation.set_function("setBool", [&ctx](const ScriptEntity& entity, const std::string& name, bool value) {
            return ctx.animationService ? ctx.animationService->setBool(entity.value, name, value) : false;
        });
        animation.set_function("setTrigger", [&ctx](const ScriptEntity& entity, const std::string& name) {
            return ctx.animationService ? ctx.animationService->setTrigger(entity.value, name) : false;
        });
        // getFloat/getBool keep the get prefix deliberately: they are
        // parameterized lookups symmetric with setFloat/setBool (spec
        // section 1, symmetric-pair rule).
        animation.set_function("getFloat", [&ctx](const ScriptEntity& entity, const std::string& name) {
            return ctx.animationService ? ctx.animationService->getFloat(entity.value, name) : 0.0f;
        });
        animation.set_function("getBool", [&ctx](const ScriptEntity& entity, const std::string& name) {
            return ctx.animationService ? ctx.animationService->getBool(entity.value, name) : false;
        });
        animation.set_function("currentState", [&ctx](sol::this_state luaState, const ScriptEntity& entity) {
            sol::state_view lua(luaState);
            auto out = lua.create_table();
            if (!ctx.animationService)
                return out;
            const auto state         = ctx.animationService->controllerState(entity.value);
            out["valid"]             = state.valid;
            out["currentState"]      = state.currentState;
            out["nextState"]         = state.nextState;
            out["transitioning"]     = state.transitioning;
            out["transitionProgress"] = state.transitionProgress;
            out["normalizedTime"]    = state.normalizedTime;
            return out;
        });
    }
} // namespace vultra
