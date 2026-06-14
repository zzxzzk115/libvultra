#include "vultra/function/scripting/bindings/script_audio_shim.hpp"

#include "vultra/core/base/uuid.hpp"

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

    bool audioPreloadClip(ScriptContext& ctx, const std::string& clip)
    {
        if (!ctx.audioService)
            return false;
        return isUri(clip) ? ctx.audioService->preloadClip(std::string_view {clip}) :
                             ctx.audioService->preloadClip(parseUuid(clip));
    }

    SoundId audioPlayOneShot(ScriptContext& ctx, const std::string& clip,
                             sol::optional<float> volume, sol::optional<float> pitch)
    {
        if (!ctx.audioService)
            return kInvalidSoundId;
        const float vol = volume.value_or(1.0f);
        const float pit = pitch.value_or(1.0f);
        return isUri(clip) ? ctx.audioService->playOneShot(std::string_view {clip}, vol, pit) :
                             ctx.audioService->playOneShot(parseUuid(clip), vol, pit);
    }

    SoundId audioPlayOneShotAt(ScriptContext& ctx, const std::string& clip, const glm::vec3& position,
                               sol::optional<float> volume, sol::optional<float> pitch)
    {
        if (!ctx.audioService)
            return kInvalidSoundId;
        const float vol = volume.value_or(1.0f);
        const float pit = pitch.value_or(1.0f);
        return isUri(clip) ? ctx.audioService->playOneShotAt(std::string_view {clip}, position, vol, pit) :
                             ctx.audioService->playOneShotAt(parseUuid(clip), position, vol, pit);
    }

    SoundId audioPlayMusic(ScriptContext& ctx, const std::string& clip, sol::optional<sol::table> options)
    {
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
    }

    void audioStopMusic(ScriptContext& ctx, sol::optional<float> fadeOutMs)
    {
        if (ctx.audioService)
            ctx.audioService->stopMusic(fadeOutMs.value_or(0.0f));
    }

    void audioStopSound(ScriptContext& ctx, SoundId id, sol::optional<float> fadeOutMs)
    {
        if (ctx.audioService)
            ctx.audioService->stop(id, fadeOutMs.value_or(0.0f));
    }

    void audioPauseSound(ScriptContext& ctx, SoundId id)
    {
        if (ctx.audioService)
            ctx.audioService->pause(id);
    }

    void audioResumeSound(ScriptContext& ctx, SoundId id)
    {
        if (ctx.audioService)
            ctx.audioService->resume(id);
    }

    void audioSetVolume(ScriptContext& ctx, SoundId id, float volume)
    {
        if (ctx.audioService)
            ctx.audioService->setVolume(id, volume);
    }

    void audioSetPitch(ScriptContext& ctx, SoundId id, float pitch)
    {
        if (ctx.audioService)
            ctx.audioService->setPitch(id, pitch);
    }

    void audioSetLooping(ScriptContext& ctx, SoundId id, bool loop)
    {
        if (ctx.audioService)
            ctx.audioService->setLooping(id, loop);
    }

    bool audioIsPlaying(ScriptContext& ctx, SoundId id)
    {
        return ctx.audioService && ctx.audioService->isPlaying(id);
    }

    bool audioPlay(ScriptContext& ctx, const ScriptEntity& entity, sol::optional<bool> restart)
    {
        return ctx.audioService ? ctx.audioService->play(entity.value, restart.value_or(false)) : false;
    }

    bool audioPause(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return ctx.audioService ? ctx.audioService->pause(entity.value) : false;
    }

    bool audioStop(ScriptContext& ctx, const ScriptEntity& entity)
    {
        return ctx.audioService ? ctx.audioService->stop(entity.value) : false;
    }

    void audioSetMasterVolume(ScriptContext& ctx, float volume)
    {
        if (ctx.audioService)
            ctx.audioService->setMasterVolume(volume);
    }

    float audioMasterVolume(ScriptContext& ctx)
    {
        return ctx.audioService ? ctx.audioService->masterVolume() : 0.0f;
    }

    bool audioBackendReady(ScriptContext& ctx)
    {
        return ctx.audioService && ctx.audioService->backendReady();
    }
} // namespace vultra
