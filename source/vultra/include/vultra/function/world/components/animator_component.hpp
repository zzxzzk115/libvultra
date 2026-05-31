#pragma once

#include "vultra/core/base/uuid.hpp"

namespace vultra
{
    struct AnimatorComponent
    {
        CoreUUID skeleton;
        CoreUUID animation;

        bool  playOnStart {true};
        bool  playing {true};
        bool  loop {true};
        float speed {1.0f};
        float time {0.0f};
    };
} // namespace vultra
