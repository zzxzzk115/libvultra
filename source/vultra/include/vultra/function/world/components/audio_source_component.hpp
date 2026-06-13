#pragma once

#include "vultra/core/base/lua_annotations.hpp"
#include "vultra/core/base/uuid.hpp"

namespace vultra
{
    // Plays a cooked audio clip (vaudio asset) at the entity. Runtime sound instances
    // live in AudioSystem; this component only stores the desired state, which the
    // system reconciles every frame (including stopping sounds of destroyed entities).
    struct VLUA_CLASS(name = AudioSource, ref = ScriptAudioSourceRef, accessor = audioSource) AudioSourceComponent
    {
        VLUA_FIELD() CoreUUID clip;

        VLUA_FIELD() float volume {1.0f};
        VLUA_FIELD() float pitch {1.0f};
        VLUA_FIELD() bool  loop {false};
        VLUA_FIELD() bool  playOnStart {true};
        VLUA_FIELD() bool  playing {false};

        // 3D spatialization driven by the entity's TransformComponent. When false the
        // clip plays as a plain 2D/UI sound.
        VLUA_FIELD() bool  spatial {true};
        VLUA_FIELD() float minDistance {1.0f};
        VLUA_FIELD() float maxDistance {100.0f};
        VLUA_FIELD() float rolloff {1.0f};
    };
} // namespace vultra
