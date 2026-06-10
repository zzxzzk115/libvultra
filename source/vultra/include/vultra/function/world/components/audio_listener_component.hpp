#pragma once

namespace vultra
{
    // Marks the entity whose TransformComponent drives the 3D audio listener pose.
    // The first entity with primary=true wins; without any listener the engine keeps
    // the default pose (origin, -Z forward).
    struct AudioListenerComponent
    {
        bool primary {true};
    };
} // namespace vultra
