#pragma once

// Shim declarations for the Lua `Audio` namespace. The IR pipeline generates the
// sol2 registration (script_audio_binding.gen.cpp) + stub; the bodies in
// script_audio_shim.cpp own the UUID-or-uri addressing, options-table parsing,
// and sol::optional defaulting.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/audio_service.hpp"

#include <glm/vec3.hpp>
#include <sol/sol.hpp>

#include <string>

namespace vultra
{
    struct VBIND_MODULE(name = Audio, service = audioService) AudioModule
    {
    };

    VBIND_FN(module = Audio, name = preloadClip, body = shim)
    bool audioPreloadClip(ScriptContext& ctx, const std::string& clip);

    VBIND_FN(module = Audio, name = playOneShot, body = shim)
    SoundId audioPlayOneShot(ScriptContext& ctx, const std::string& clip,
                             sol::optional<float> volume, sol::optional<float> pitch);

    VBIND_FN(module = Audio, name = playOneShotAt, body = shim)
    SoundId audioPlayOneShotAt(ScriptContext& ctx, const std::string& clip, const glm::vec3& position,
                               sol::optional<float> volume, sol::optional<float> pitch);

    VBIND_FN(module = Audio, name = playMusic, body = shim)
    SoundId audioPlayMusic(ScriptContext& ctx, const std::string& clip, sol::optional<sol::table> options);

    VBIND_FN(module = Audio, name = stopMusic, body = shim)
    void audioStopMusic(ScriptContext& ctx, sol::optional<float> fadeOutMs);

    VBIND_FN(module = Audio, name = stopSound, body = shim)
    void audioStopSound(ScriptContext& ctx, SoundId id, sol::optional<float> fadeOutMs);

    VBIND_FN(module = Audio, name = pauseSound, body = shim)
    void audioPauseSound(ScriptContext& ctx, SoundId id);

    VBIND_FN(module = Audio, name = resumeSound, body = shim)
    void audioResumeSound(ScriptContext& ctx, SoundId id);

    VBIND_FN(module = Audio, name = setVolume, body = shim)
    void audioSetVolume(ScriptContext& ctx, SoundId id, float volume);

    VBIND_FN(module = Audio, name = setPitch, body = shim)
    void audioSetPitch(ScriptContext& ctx, SoundId id, float pitch);

    VBIND_FN(module = Audio, name = setLooping, body = shim)
    void audioSetLooping(ScriptContext& ctx, SoundId id, bool loop);

    VBIND_FN(module = Audio, name = isPlaying, body = shim)
    bool audioIsPlaying(ScriptContext& ctx, SoundId id);

    VBIND_FN(module = Audio, name = play, body = shim)
    bool audioPlay(ScriptContext& ctx, const ScriptEntity& entity, sol::optional<bool> restart);

    VBIND_FN(module = Audio, name = pause, body = shim)
    bool audioPause(ScriptContext& ctx, const ScriptEntity& entity);

    VBIND_FN(module = Audio, name = stop, body = shim)
    bool audioStop(ScriptContext& ctx, const ScriptEntity& entity);

    VBIND_FN(module = Audio, name = setMasterVolume, body = shim)
    void audioSetMasterVolume(ScriptContext& ctx, float volume);

    VBIND_FN(module = Audio, name = masterVolume, body = shim)
    float audioMasterVolume(ScriptContext& ctx);

    VBIND_FN(module = Audio, name = backendReady, body = shim)
    bool audioBackendReady(ScriptContext& ctx);
} // namespace vultra
