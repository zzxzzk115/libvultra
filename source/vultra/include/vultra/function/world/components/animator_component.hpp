#pragma once

#include "vultra/core/base/uuid.hpp"

#include <cstdint>
#include <string>

namespace vultra
{
    // Drives skeletal animation in one of two modes:
    //   mode 0 (Single Clip): plays the `animation` clip directly.
    //   mode 1 (Graph): runs the animator graph at `graph` (a .vanimgraph.json) - a state
    //                   machine with parameters, transitions, and cross-fade blending.
    // The skeleton is optional; when unset it defaults to the entity's (or a descendant's)
    // skinned-mesh bundled skeleton.
    struct AnimatorComponent
    {
        uint32_t mode {0}; // 0 = single clip, 1 = animator graph

        CoreUUID skeleton;

        // --- Single-clip mode ---
        CoreUUID animation;
        bool     playOnStart {true};
        bool     playing {true};
        bool     loop {true};
        float    speed {1.0f};
        float    time {0.0f};

        // --- Graph mode ---
        std::string graph; // res:// URI of the .vanimgraph.json

        // Root motion (graph mode): when true, the playing clip's root-joint horizontal
        // translation drives the entity transform instead of sliding the mesh in place.
        bool applyRootMotion {false};
    };
} // namespace vultra
