#pragma once

#include "vultra/core/base/uuid.hpp"

namespace vultra
{
    // Plays a cooked audio clip (vaudio asset) at the entity. Runtime sound instances
    // live in AudioSystem; this component only stores the desired state, which the
    // system reconciles every frame (including stopping sounds of destroyed entities).
    struct AudioSourceComponent
    {
        CoreUUID clip;

        float volume {1.0f};
        float pitch {1.0f};
        bool  loop {false};
        bool  playOnStart {true};
        bool  playing {false};

        // 3D spatialization driven by the entity's TransformComponent. When false the
        // clip plays as a plain 2D/UI sound.
        bool  spatial {true};
        float minDistance {1.0f};
        float maxDistance {100.0f};
        float rolloff {1.0f};
    };
} // namespace vultra
