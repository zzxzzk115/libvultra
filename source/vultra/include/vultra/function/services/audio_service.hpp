#pragma once

#include "vultra/core/base/uuid.hpp"

#include <vbase/service/service_registry.hpp>

#include <entt/entity/fwd.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string_view>

namespace vultra
{
    // Handle for a playing sound instance (one-shots, music). 0 is invalid.
    using SoundId = uint64_t;
    constexpr SoundId kInvalidSoundId = 0;

    struct AudioPlayParams
    {
        float volume {1.0f};
        float pitch {1.0f};
        bool  loop {false};
        float fadeInMs {0.0f};
    };

    // Audio service interface.
    // Threading: all methods must be called from the main engine thread. miniaudio's
    // mixer runs on its own device thread; the parameter setters used here are safe
    // against it, but the service itself is not re-entrant across engine threads.
    class IAudioService
    {
    public:
        SERVICE_REGISTER(IAudioService)

        virtual ~IAudioService() = default;

        // --- clips (cooked vaudio assets, by registry UUID or res:// source/imported uri) ---
        virtual bool preloadClip(const CoreUUID& uuid) = 0;
        virtual bool preloadClip(std::string_view uri) = 0;
        virtual void unloadClip(const CoreUUID& uuid) = 0;

        // --- fire-and-forget playback (auto-recycled when finished) ---
        virtual SoundId playOneShot(const CoreUUID& uuid, float volume = 1.0f, float pitch = 1.0f) = 0;
        virtual SoundId playOneShot(std::string_view uri, float volume = 1.0f, float pitch = 1.0f) = 0;
        virtual SoundId
        playOneShotAt(const CoreUUID& uuid, const glm::vec3& position, float volume = 1.0f, float pitch = 1.0f) = 0;
        virtual SoundId
        playOneShotAt(std::string_view uri, const glm::vec3& position, float volume = 1.0f, float pitch = 1.0f) = 0;

        // --- music (single slot; starting a new track replaces the current one) ---
        virtual SoundId playMusic(const CoreUUID& uuid, const AudioPlayParams& params = {.loop = true}) = 0;
        virtual SoundId playMusic(std::string_view uri, const AudioPlayParams& params = {.loop = true}) = 0;
        virtual void    stopMusic(float fadeOutMs = 0.0f) = 0;

        // --- handle-based control ---
        virtual void stop(SoundId id, float fadeOutMs = 0.0f) = 0;
        virtual void pause(SoundId id) = 0;
        virtual void resume(SoundId id) = 0;
        virtual void setVolume(SoundId id, float volume) = 0;
        virtual void setPitch(SoundId id, float pitch) = 0;
        virtual void setLooping(SoundId id, bool loop) = 0;
        [[nodiscard]] virtual bool isPlaying(SoundId id) const = 0;

        // --- ECS AudioSourceComponent control (mirrors IAnimationService's entity API) ---
        virtual bool play(entt::entity entity, bool restart = false) = 0;
        virtual bool pause(entt::entity entity) = 0;
        virtual bool stop(entt::entity entity) = 0;

        // --- global ---
        virtual void  setMasterVolume(float volume) = 0;
        [[nodiscard]] virtual float masterVolume() const = 0;

        // Editor play-mode gating (same contract as animation/script/physics services).
        virtual void setPlaybackState(bool playing, bool paused) = 0;

        // False while the audio device is unavailable, and on web until the first user
        // gesture resumes the AudioContext. Playback requests are still accepted; they
        // become audible once the backend is running.
        [[nodiscard]] virtual bool backendReady() const = 0;

        // Diagnostics/testing: number of live sound instances owned by the system.
        [[nodiscard]] virtual size_t debugActiveSoundCount() const = 0;
    };
} // namespace vultra
