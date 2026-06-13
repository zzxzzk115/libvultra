#pragma once

#include "vultra/core/base/lua_annotations.hpp"

namespace vultra
{
    // Marks the entity whose TransformComponent drives the 3D audio listener pose.
    // The first entity with primary=true wins; without any listener the engine keeps
    // the default pose (origin, -Z forward).
    struct VLUA_CLASS(name = AudioListener, ref = ScriptAudioListenerRef, accessor = audioListener)
        AudioListenerComponent
    {
        VLUA_FIELD() bool primary {true};
    };
} // namespace vultra
