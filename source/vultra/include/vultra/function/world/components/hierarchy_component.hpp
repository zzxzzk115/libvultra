#pragma once

#include <entt/entity/entity.hpp>

namespace vultra
{
    // Intrusive entity tree links.
    // World owns structural operations (attach/detach/destroyRecursive).
    struct HierarchyComponent
    {
        entt::entity parent {entt::null};

        entt::entity firstChild {entt::null};
        entt::entity nextSibling {entt::null};
        entt::entity prevSibling {entt::null};

        uint32_t childCount {0};
    };
} // namespace vultra
