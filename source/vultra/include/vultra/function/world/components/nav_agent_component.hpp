#pragma once

#include "vultra/core/base/script_annotations.hpp"

#include <glm/vec3.hpp>

namespace vultra
{
    // Drives an entity along a path on the baked navmesh. The NavigationSystem fills the path
    // from `targetPosition` and steers the entity (feeding a CharacterControllerComponent when
    // present, otherwise moving the transform directly). Set the destination via
    // Nav.setAgentDestination(entity, target).
    struct VBIND_USERTYPE(name = NavAgent, handle = ScriptNavAgentRef, component = NavAgentComponent,
                          accessor = navAgent) NavAgentComponent
    {
        VBIND_FIELD() float radius {0.4f};           // agent radius (m) used for path queries
        VBIND_FIELD() float height {1.8f};           // agent height (m)
        VBIND_FIELD() float speed {3.5f};            // movement speed (m/s)
        VBIND_FIELD() float stoppingDistance {0.3f}; // stop when within this distance of the target

        // Set by gameplay (Nav.setAgentDestination); cleared on arrival / Nav.stopAgent.
        VBIND_FIELD() glm::vec3 targetPosition {0.0f};
        VBIND_FIELD() bool      hasTarget {false};

        // Runtime output (not authored): true while a path is being followed.
        VBIND_FIELD(readonly) bool moving {false};
    };
} // namespace vultra
