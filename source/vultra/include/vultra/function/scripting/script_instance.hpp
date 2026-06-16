#pragma once

#include <entt/entity/entity.hpp>
#include <sol/sol.hpp>

#include <string>

namespace vultra
{
    struct ScriptInstance
    {
        entt::entity entity {entt::null};

        sol::environment env;

        // Lifecycle order: OnCreate -> OnEnable -> OnUpdate/OnFixedUpdate...
        // -> OnDisable -> OnDestroy (doc/lua_scripting.md).
        sol::protected_function onCreate;
        sol::protected_function onDestroy;
        sol::protected_function onUpdate;
        sol::protected_function onFixedUpdate;
        sol::protected_function onEnable;
        sol::protected_function onDisable;

        // Contact callbacks dispatched by ScriptSystem from physics
        // contact-pair diffing; `other` is the colliding entity.
        sol::protected_function onCollisionEnter;
        sol::protected_function onCollisionStay;
        sol::protected_function onCollisionExit;
        sol::protected_function onTriggerEnter;
        sol::protected_function onTriggerStay;
        sol::protected_function onTriggerExit;

        // Keyframe animation event, dispatched by ScriptSystem when the animation
        // system crosses a state event time: OnAnimationEvent(self, name).
        sol::protected_function onAnimationEvent;

        std::string loadedUri;
        bool        valid {false};
        bool        enabled {true};
        // whether OnEnable has been dispatched without a matching OnDisable
        bool enabledActive {false};

        explicit ScriptInstance(sol::environment&& environment) : env(std::move(environment)) {}
    };
} // namespace vultra
