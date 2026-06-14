#pragma once

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/core/base/uuid.hpp"

namespace vultra
{
    // Plays a cooked audio clip (vaudio asset) at the entity. Runtime sound instances
    // live in AudioSystem; this component only stores the desired state, which the
    // system reconciles every frame (including stopping sounds of destroyed entities).
    struct VBIND_USERTYPE(name = AudioSource, handle = ScriptAudioSourceRef, accessor = audioSource) AudioSourceComponent
    {
        VBIND_FIELD() CoreUUID clip;

        VBIND_FIELD() float volume {1.0f};
        VBIND_FIELD() float pitch {1.0f};
        VBIND_FIELD() bool  loop {false};
        VBIND_FIELD() bool  playOnStart {true};
        VBIND_FIELD() bool  playing {false};

        // 3D spatialization driven by the entity's TransformComponent. When false the
        // clip plays as a plain 2D/UI sound.
        VBIND_FIELD() bool  spatial {true};
        VBIND_FIELD() float minDistance {1.0f};
        VBIND_FIELD() float maxDistance {100.0f};
        VBIND_FIELD() float rolloff {1.0f};
    };
} // namespace vultra
