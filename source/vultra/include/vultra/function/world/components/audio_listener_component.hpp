#pragma once

#include "vultra/core/base/script_annotations.hpp"

namespace vultra
{
    // Marks the entity whose TransformComponent drives the 3D audio listener pose.
    // The first entity with primary=true wins; without any listener the engine keeps
    // the default pose (origin, -Z forward).
    struct VBIND_USERTYPE(name = AudioListener, handle = ScriptAudioListenerRef)
        AudioListenerComponent
    {
        VBIND_FIELD() bool primary {true};
    };
} // namespace vultra
