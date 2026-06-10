#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/audio_service.hpp"

#include <memory>

namespace vultra
{
    class IAssetService;
    class IWorldService;
    class ICameraService;

    // miniaudio-backed audio subsystem.
    //  - Engine/mixing: ma_engine (WASAPI/CoreAudio/ALSA/AAudio; Web Audio on wasm).
    //  - Clips: cooked vaudio assets loaded through IAssetService and registered with
    //    miniaudio's resource manager (PCM as decoded data, passthrough as encoded data).
    //  - Spatialization: AudioSource/AudioListener components synced in onPreRender,
    //    after WorldSystem refreshed TransformComponent::worldMatrix.
    // If the audio device cannot be initialized (headless/CI) the system stays in an
    // audio-less mode: all calls are safe no-ops and backendReady() returns false.
    class AudioSystem final : public EngineSubsystem, public IAudioService
    {
    public:
        ENGINE_SUBSYSTEM(AudioSystem)

        AudioSystem();
        ~AudioSystem() override;

        bool onInit() override;
        void onShutdown() override;
        void onUpdate(fsec dt) override;
        void onPreRender() override;

        bool preloadClip(const CoreUUID& uuid) override;
        bool preloadClip(std::string_view uri) override;
        void unloadClip(const CoreUUID& uuid) override;

        SoundId playOneShot(const CoreUUID& uuid, float volume = 1.0f, float pitch = 1.0f) override;
        SoundId playOneShot(std::string_view uri, float volume = 1.0f, float pitch = 1.0f) override;
        SoundId playOneShotAt(const CoreUUID& uuid,
                              const glm::vec3& position,
                              float            volume = 1.0f,
                              float            pitch  = 1.0f) override;
        SoundId playOneShotAt(std::string_view uri,
                              const glm::vec3& position,
                              float            volume = 1.0f,
                              float            pitch  = 1.0f) override;

        SoundId playMusic(const CoreUUID& uuid, const AudioPlayParams& params = {.loop = true}) override;
        SoundId playMusic(std::string_view uri, const AudioPlayParams& params = {.loop = true}) override;
        void    stopMusic(float fadeOutMs = 0.0f) override;

        void stop(SoundId id, float fadeOutMs = 0.0f) override;
        void pause(SoundId id) override;
        void resume(SoundId id) override;
        void setVolume(SoundId id, float volume) override;
        void setPitch(SoundId id, float pitch) override;
        void setLooping(SoundId id, bool loop) override;
        bool isPlaying(SoundId id) const override;

        bool play(entt::entity entity, bool restart = false) override;
        bool pause(entt::entity entity) override;
        bool stop(entt::entity entity) override;

        void  setMasterVolume(float volume) override;
        float masterVolume() const override;
        void  setPlaybackState(bool playing, bool paused) override;
        bool  backendReady() const override;

        size_t debugActiveSoundCount() const override;

    private:
        // All miniaudio types stay behind this pimpl so miniaudio.h never leaks into
        // public engine headers.
        struct Impl;
        std::unique_ptr<Impl> m_Impl;

        IAssetService*  m_Assets {nullptr};
        IWorldService*  m_Worlds {nullptr};
        ICameraService* m_Cameras {nullptr};

        bool m_PlaybackPlaying {true};
        bool m_PlaybackPaused {false};
    };
} // namespace vultra
