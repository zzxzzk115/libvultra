#pragma once

#include <entt/entity/entity.hpp>

namespace vultra
{
    struct ScriptEntity
    {
        entt::entity value {entt::null};
    };

    struct ScriptVec2
    {
        float x {0.0f};
        float y {0.0f};
    };

    struct ScriptVec3
    {
        float x {0.0f};
        float y {0.0f};
        float z {0.0f};
    };
} // namespace vultra
