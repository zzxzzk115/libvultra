#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace vultra
{
    // Numeric values mirror Jolt's common rigid body choices for stable .vscn serialization.
    // motionType: 0 = static, 1 = kinematic, 2 = dynamic.
    // motionQuality: 0 = discrete, 1 = linear cast.
    struct RigidBodyComponent
    {
        uint32_t motionType {2};
        uint32_t objectLayer {0}; // physics layer index (collision-matrix); 0 = "Default"
        bool     isSensor {false};
        uint32_t motionQuality {0};
        bool     allowSleeping {true};

        float friction {0.2f};
        float restitution {0.0f};
        float linearDamping {0.05f};
        float angularDamping {0.05f};
        float gravityFactor {1.0f};

        glm::vec3 linearVelocity {0.0f};
        glm::vec3 angularVelocity {0.0f};

        float mass {1.0f};
        bool  overrideMass {false};
        float maxLinearVelocity {500.0f};
        float maxAngularVelocity {0.25f * 3.1415926535f * 60.0f};
    };
} // namespace vultra
