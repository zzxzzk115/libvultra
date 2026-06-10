#include "vultra/function/scripting/bindings/script_audio_binding.hpp"

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/audio_service.hpp"

#include <glm/vec3.hpp>
#include <sol/sol.hpp>

#include <string>

namespace vultra
{
    namespace
    {
        CoreUUID parseUuid(const std::string& value)
        {
            vbase::UUID uuid {};
            vbase::try_parse_uuid(value.c_str(), uuid);
            return CoreUUID(uuid);
        }

        // Clips are addressed by either a registry UUID string or a res:// uri.
        bool isUri(const std::string& value) { return value.find("://") != std::string::npos; }
    } // namespace

    void registerScriptAudioBindings(sol::state& lua, ScriptContext& ctx)
    {
        auto audio = script_binding::getOrCreateTable(lua, "Audio");

        audio.set_function("preloadClip", [&ctx](const std::string& clip) {
            if (!ctx.audioService)
                return false;
            return isUri(clip) ? ctx.audioService->preloadClip(std::string_view {clip}) :
                                 ctx.audioService->preloadClip(parseUuid(clip));
        });

        audio.set_function(
            "playOneShot", [&ctx](const std::string& clip, sol::optional<float> volume, sol::optional<float> pitch) {
                if (!ctx.audioService)
                    return kInvalidSoundId;
                const float vol = volume.value_or(1.0f);
                const float pit = pitch.value_or(1.0f);
                return isUri(clip) ? ctx.audioService->playOneShot(std::string_view {clip}, vol, pit) :
                                     ctx.audioService->playOneShot(parseUuid(clip), vol, pit);
            });

        audio.set_function("playOneShotAt",
                           [&ctx](const std::string&  clip,
                                  const glm::vec3&    position,
                                  sol::optional<float> volume,
                                  sol::optional<float> pitch) {
                               if (!ctx.audioService)
                                   return kInvalidSoundId;
                               const float vol = volume.value_or(1.0f);
                               const float pit = pitch.value_or(1.0f);
                               return isUri(clip) ?
                                          ctx.audioService->playOneShotAt(std::string_view {clip}, position, vol, pit) :
                                          ctx.audioService->playOneShotAt(parseUuid(clip), position, vol, pit);
                           });

        // playMusic(clip, { volume=1, pitch=1, loop=true, fadeInMs=0 })
        audio.set_function("playMusic", [&ctx](const std::string& clip, sol::optional<sol::table> options) {
            if (!ctx.audioService)
                return kInvalidSoundId;
            AudioPlayParams params {.loop = true};
            if (options)
            {
                params.volume   = options->get_or("volume", params.volume);
                params.pitch    = options->get_or("pitch", params.pitch);
                params.loop     = options->get_or("loop", params.loop);
                params.fadeInMs = options->get_or("fadeInMs", params.fadeInMs);
            }
            return isUri(clip) ? ctx.audioService->playMusic(std::string_view {clip}, params) :
                                 ctx.audioService->playMusic(parseUuid(clip), params);
        });

        audio.set_function("stopMusic", [&ctx](sol::optional<float> fadeOutMs) {
            if (ctx.audioService)
                ctx.audioService->stopMusic(fadeOutMs.value_or(0.0f));
        });

        audio.set_function("stopSound", [&ctx](SoundId id, sol::optional<float> fadeOutMs) {
            if (ctx.audioService)
                ctx.audioService->stop(id, fadeOutMs.value_or(0.0f));
        });
        audio.set_function("pauseSound", [&ctx](SoundId id) {
            if (ctx.audioService)
                ctx.audioService->pause(id);
        });
        audio.set_function("resumeSound", [&ctx](SoundId id) {
            if (ctx.audioService)
                ctx.audioService->resume(id);
        });
        audio.set_function("setVolume", [&ctx](SoundId id, float volume) {
            if (ctx.audioService)
                ctx.audioService->setVolume(id, volume);
        });
        audio.set_function("setPitch", [&ctx](SoundId id, float pitch) {
            if (ctx.audioService)
                ctx.audioService->setPitch(id, pitch);
        });
        audio.set_function("setLooping", [&ctx](SoundId id, bool loop) {
            if (ctx.audioService)
                ctx.audioService->setLooping(id, loop);
        });
        audio.set_function(
            "isPlaying", [&ctx](SoundId id) { return ctx.audioService && ctx.audioService->isPlaying(id); });

        // --- AudioSourceComponent control (entity-based, like Animation.play/pause/stop) ---
        audio.set_function("play", [&ctx](const ScriptEntity& entity, sol::optional<bool> restart) {
            return ctx.audioService ? ctx.audioService->play(entity.value, restart.value_or(false)) : false;
        });
        audio.set_function("pause", [&ctx](const ScriptEntity& entity) {
            return ctx.audioService ? ctx.audioService->pause(entity.value) : false;
        });
        audio.set_function("stop", [&ctx](const ScriptEntity& entity) {
            return ctx.audioService ? ctx.audioService->stop(entity.value) : false;
        });

        audio.set_function("setMasterVolume", [&ctx](float volume) {
            if (ctx.audioService)
                ctx.audioService->setMasterVolume(volume);
        });
        audio.set_function("masterVolume",
                           [&ctx]() { return ctx.audioService ? ctx.audioService->masterVolume() : 0.0f; });
        audio.set_function("backendReady",
                           [&ctx]() { return ctx.audioService && ctx.audioService->backendReady(); });
    }
} // namespace vultra
