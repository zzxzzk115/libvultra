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

        sol::protected_function onCreate;
        sol::protected_function onDestroy;
        sol::protected_function onUpdate;
        sol::protected_function onFixedUpdate;

        std::string loadedUri;
        bool        valid {false};
        bool        enabled {true};

        explicit ScriptInstance(sol::environment&& environment) : env(std::move(environment)) {}
    };
} // namespace vultra
