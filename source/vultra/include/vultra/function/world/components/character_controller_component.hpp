#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace vultra
{
    // Kinematic character controller backed by Jolt's CharacterVirtual. Suitable for
    // FPS/TPS player and AI movement: collide-and-slide, slope limits, stair stepping,
    // and ground detection without full rigid-body dynamics.
    //
    // The controller's position is the entity's feet (capsule base). Gameplay drives it
    // through `inputMove` (desired horizontal velocity) plus `jumpRequested`/`jumpSpeed`;
    // the physics system applies gravity, resolves collisions, and writes back `velocity`
    // and `grounded` each step.
    struct CharacterControllerComponent
    {
        // Capsule shape (total height must be >= 2 * radius).
        float radius {0.3f};
        float height {1.8f};

        // Movement tuning.
        float    maxSlopeAngleDegrees {45.0f};
        float    stepHeight {0.3f};
        float    gravityFactor {1.0f};
        float    mass {70.0f};
        float    jumpSpeed {5.0f};
        uint32_t objectLayer {1};

        // Input (gameplay-controlled, consumed each step).
        glm::vec3 inputMove {0.0f}; // desired horizontal velocity (x, z used)
        bool      jumpRequested {false};

        // Output (physics-updated, read-only for gameplay).
        glm::vec3 velocity {0.0f};
        bool      grounded {false};
    };
} // namespace vultra
