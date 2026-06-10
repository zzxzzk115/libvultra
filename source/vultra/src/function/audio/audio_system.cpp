#include "vultra/function/audio/audio_system.hpp"

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/os/window.hpp"
#include "vultra/core/services/window_service.hpp"
#include "vultra/function/rendering/render_structs.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/camera_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/audio_listener_component.hpp"
#include "vultra/function/world/components/audio_source_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

#include <vasset/vaudio.hpp>

#include <miniaudio.h>

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

namespace vultra
{
    namespace
    {
        ma_format toMaFormat(vasset::VAudioStorage storage)
        {
            return storage == vasset::VAudioStorage::ePCMF32 ? ma_format_f32 : ma_format_s16;
        }
    } // namespace

    struct AudioSystem::Impl
    {
        ma_resource_manager resourceManager {};
        ma_engine           engine {};
        bool                resourceManagerValid {false};
        bool                engineValid {false};

        // True once the device is running. On web this flips after the first user
        // gesture resumes the AudioContext.
        bool deviceStarted {false};

        float masterVolume {1.0f};

        // Guards the window-event subscription, which may outlive this subsystem's
        // shutdown (the window is registered earlier and torn down later).
        std::shared_ptr<bool> alive {std::make_shared<bool>(true)};

        struct ClipRuntime
        {
            AssetHandle<vasset::VAudio, resource::CpuAsset> handle;
            std::string                                     regName;
            bool                                            registered {false};
        };
        // The clip cache owns the AssetHandles: the cooked bytes must outlive every
        // ma_sound created from them because miniaudio does not copy registered data.
        std::unordered_map<CoreUUID, ClipRuntime> clips;

        enum class SoundKind
        {
            eOneShot,
            eMusic,
            eComponent,
        };

        struct ActiveSound
        {
            std::unique_ptr<ma_sound> sound;
            SoundKind                 kind {SoundKind::eOneShot};
            entt::entity              owner {entt::null};
            CoreUUID                  clip;
            bool                      fadingOut {false};
            bool                      pausedByPlayback {false};
        };
        std::unordered_map<SoundId, ActiveSound> sounds;

        // Per-entity reconcile state for AudioSourceComponents.
        struct EntitySoundState
        {
            SoundId id {kInvalidSoundId};
            bool    seeded {false};

            // Cached component fields for dirty-checking.
            float volume {1.0f};
            float pitch {1.0f};
            bool  loop {false};
            bool  spatial {true};
            float minDistance {1.0f};
            float maxDistance {100.0f};
            float rolloff {1.0f};
        };
        std::unordered_map<entt::entity, EntitySoundState> entitySounds;

        SoundId musicId {kInvalidSoundId};
        SoundId nextId {1};

        ActiveSound* find(SoundId id)
        {
            auto it = sounds.find(id);
            return it == sounds.end() ? nullptr : &it->second;
        }

        const ActiveSound* find(SoundId id) const
        {
            auto it = sounds.find(id);
            return it == sounds.end() ? nullptr : &it->second;
        }

        void destroySound(SoundId id)
        {
            auto it = sounds.find(id);
            if (it == sounds.end())
                return;
            if (it->second.sound)
            {
                ma_sound_stop(it->second.sound.get());
                ma_sound_uninit(it->second.sound.get());
            }
            if (musicId == id)
                musicId = kInvalidSoundId;
            sounds.erase(it);
        }

        void startDevice()
        {
            if (!engineValid || deviceStarted)
                return;
            if (ma_engine_start(&engine) == MA_SUCCESS)
                deviceStarted = true;
        }

        bool registerClipData(ClipRuntime& clip)
        {
            const auto* cpu = clip.handle.cpu();
            if (!cpu || cpu->audioData.empty())
                return false;

            ma_result result = MA_ERROR;
            if (vasset::isPassthrough(cpu->storage))
            {
                result = ma_resource_manager_register_encoded_data(
                    &resourceManager, clip.regName.c_str(), cpu->audioData.data(), cpu->audioData.size());
            }
            else
            {
                result = ma_resource_manager_register_decoded_data(&resourceManager,
                                                                   clip.regName.c_str(),
                                                                   cpu->audioData.data(),
                                                                   cpu->frameCount,
                                                                   toMaFormat(cpu->storage),
                                                                   cpu->channels,
                                                                   cpu->sampleRate);
            }
            clip.registered = result == MA_SUCCESS;
            return clip.registered;
        }

        ClipRuntime* ensureClip(AssetHandle<vasset::VAudio, resource::CpuAsset> handle)
        {
            if (!handle.ready() || !handle.cpu())
                return nullptr;

            const CoreUUID uuid = handle.uuid();
            auto           it   = clips.find(uuid);
            if (it != clips.end())
                return it->second.registered ? &it->second : nullptr;

            ClipRuntime clip {};
            clip.handle  = std::move(handle);
            clip.regName = "vaudio://" + uuid.toString();
            if (!registerClipData(clip))
            {
                VULTRA_CLIENT_ERROR("[AudioSystem] failed to register clip {}", uuid.toString());
                return nullptr;
            }
            return &clips.emplace(uuid, std::move(clip)).first->second;
        }

        struct SpawnDesc
        {
            SoundKind    kind {SoundKind::eOneShot};
            bool         spatial {false};
            float        volume {1.0f};
            float        pitch {1.0f};
            bool         loop {false};
            float        fadeInMs {0.0f};
            entt::entity owner {entt::null};
        };

        SoundId spawnSound(ClipRuntime* clip, const CoreUUID& clipUuid, const SpawnDesc& desc, bool start)
        {
            if (!clip)
                return kInvalidSoundId;

            auto      sound = std::make_unique<ma_sound>();
            ma_uint32 flags = MA_SOUND_FLAG_DECODE;
            if (!desc.spatial)
                flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
            if (ma_sound_init_from_file(&engine, clip->regName.c_str(), flags, nullptr, nullptr, sound.get()) !=
                MA_SUCCESS)
            {
                VULTRA_CLIENT_ERROR("[AudioSystem] failed to create sound for clip {}", clipUuid.toString());
                return kInvalidSoundId;
            }

            ma_sound_set_volume(sound.get(), std::max(desc.volume, 0.0f));
            ma_sound_set_pitch(sound.get(), std::max(desc.pitch, 0.01f));
            ma_sound_set_looping(sound.get(), desc.loop ? MA_TRUE : MA_FALSE);
            if (desc.spatial)
                ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_inverse);
            if (desc.fadeInMs > 0.0f)
                ma_sound_set_fade_in_milliseconds(sound.get(), 0.0f, 1.0f, static_cast<ma_uint64>(desc.fadeInMs));
            if (start)
                ma_sound_start(sound.get());

            const SoundId id = nextId++;
            ActiveSound   active {};
            active.sound = std::move(sound);
            active.kind  = desc.kind;
            active.owner = desc.owner;
            active.clip  = clipUuid;
            sounds.emplace(id, std::move(active));
            return id;
        }
    };

    AudioSystem::AudioSystem() : m_Impl(std::make_unique<Impl>()) {}

    AudioSystem::~AudioSystem()
    {
        if (m_Impl)
            *m_Impl->alive = false;
    }

    bool AudioSystem::onInit()
    {
        ctx().services.provide<IAudioService>(this);
        m_Assets  = ctx().services.tryGet<IAssetService>();
        m_Worlds  = ctx().services.tryGet<IWorldService>();
        m_Cameras = ctx().services.tryGet<ICameraService>();

        ma_resource_manager_config rmConfig = ma_resource_manager_config_init();
#if defined(__EMSCRIPTEN__)
        // No pthreads in the wasm build: decode synchronously on the calling thread.
        rmConfig.jobThreadCount = 0;
        rmConfig.flags |= MA_RESOURCE_MANAGER_FLAG_NO_THREADING;
#endif
        if (ma_resource_manager_init(&rmConfig, &m_Impl->resourceManager) != MA_SUCCESS)
        {
            VULTRA_CORE_WARN("[AudioSystem] resource manager init failed; running audio-less");
            return true;
        }
        m_Impl->resourceManagerValid = true;

        ma_engine_config engineConfig = ma_engine_config_init();
        engineConfig.pResourceManager = &m_Impl->resourceManager;
        if (ma_engine_init(&engineConfig, &m_Impl->engine) != MA_SUCCESS)
        {
            VULTRA_CORE_WARN("[AudioSystem] engine init failed (no audio device?); running audio-less");
            ma_resource_manager_uninit(&m_Impl->resourceManager);
            m_Impl->resourceManagerValid = false;
            return true;
        }
        m_Impl->engineValid = true;

#if defined(__EMSCRIPTEN__)
        // Browsers keep the AudioContext suspended until a user gesture. GLFW/Emscripten
        // dispatches input callbacks synchronously inside the DOM gesture stack, so
        // starting the engine there performs the resume.
        if (auto* windowService = ctx().services.tryGet<IWindowService>())
        {
            auto* impl  = m_Impl.get();
            auto  alive = m_Impl->alive;
            windowService->window().on<os::GeneralWindowEvent>(
                [impl, alive](const os::GeneralWindowEvent& event, os::Window&) {
                    if (!*alive || impl->deviceStarted)
                        return;
                    if (event.type == event::WindowEventType::eKeyDown ||
                        event.type == event::WindowEventType::eMouseButtonDown)
                    {
                        impl->startDevice();
                    }
                });
        }
#else
        // ma_engine_init starts the device on desktop/mobile backends.
        m_Impl->deviceStarted = true;
#endif

        VULTRA_CORE_INFO("[AudioSystem] Initialized (sample rate {})", ma_engine_get_sample_rate(&m_Impl->engine));
        return true;
    }

    void AudioSystem::onShutdown()
    {
        *m_Impl->alive = false;

        for (auto& [id, active] : m_Impl->sounds)
        {
            if (active.sound)
            {
                ma_sound_stop(active.sound.get());
                ma_sound_uninit(active.sound.get());
            }
        }
        m_Impl->sounds.clear();
        m_Impl->entitySounds.clear();
        m_Impl->musicId = kInvalidSoundId;

        if (m_Impl->resourceManagerValid)
        {
            for (auto& [uuid, clip] : m_Impl->clips)
            {
                if (clip.registered)
                    ma_resource_manager_unregister_data(&m_Impl->resourceManager, clip.regName.c_str());
            }
        }
        m_Impl->clips.clear();

        if (m_Impl->engineValid)
        {
            ma_engine_uninit(&m_Impl->engine);
            m_Impl->engineValid = false;
        }
        if (m_Impl->resourceManagerValid)
        {
            ma_resource_manager_uninit(&m_Impl->resourceManager);
            m_Impl->resourceManagerValid = false;
        }

        m_Impl->deviceStarted = false;
        m_Assets              = nullptr;
        m_Worlds              = nullptr;
        m_Cameras             = nullptr;
        m_PlaybackPlaying     = true;
        m_PlaybackPaused      = false;
    }

    // ------------------------------------------------------------
    // Clips
    // ------------------------------------------------------------

    bool AudioSystem::preloadClip(const CoreUUID& uuid)
    {
        if (!m_Impl->engineValid || !m_Assets || !uuid.valid())
            return false;
        return m_Impl->ensureClip(m_Assets->loadAudioSync(uuid)) != nullptr;
    }

    bool AudioSystem::preloadClip(std::string_view uri)
    {
        if (!m_Impl->engineValid || !m_Assets)
            return false;
        return m_Impl->ensureClip(m_Assets->loadAudioSync(uri)) != nullptr;
    }

    void AudioSystem::unloadClip(const CoreUUID& uuid)
    {
        auto it = m_Impl->clips.find(uuid);
        if (it == m_Impl->clips.end())
            return;

        // Sounds still reading the registered data must go first.
        for (auto soundIt = m_Impl->sounds.begin(); soundIt != m_Impl->sounds.end();)
        {
            if (soundIt->second.clip == uuid)
            {
                if (soundIt->second.sound)
                {
                    ma_sound_stop(soundIt->second.sound.get());
                    ma_sound_uninit(soundIt->second.sound.get());
                }
                if (m_Impl->musicId == soundIt->first)
                    m_Impl->musicId = kInvalidSoundId;
                soundIt = m_Impl->sounds.erase(soundIt);
            }
            else
            {
                ++soundIt;
            }
        }
        for (auto entityIt = m_Impl->entitySounds.begin(); entityIt != m_Impl->entitySounds.end();)
        {
            if (entityIt->second.id != kInvalidSoundId && !m_Impl->find(entityIt->second.id))
                entityIt = m_Impl->entitySounds.erase(entityIt);
            else
                ++entityIt;
        }

        if (it->second.registered && m_Impl->resourceManagerValid)
            ma_resource_manager_unregister_data(&m_Impl->resourceManager, it->second.regName.c_str());
        m_Impl->clips.erase(it);
    }

    // ------------------------------------------------------------
    // Playback
    // ------------------------------------------------------------

    SoundId AudioSystem::playOneShot(const CoreUUID& uuid, float volume, float pitch)
    {
        if (!m_Impl->engineValid || !m_Assets || !uuid.valid())
            return kInvalidSoundId;
        auto* clip = m_Impl->ensureClip(m_Assets->loadAudioSync(uuid));
        return m_Impl->spawnSound(
            clip, uuid, {.kind = Impl::SoundKind::eOneShot, .volume = volume, .pitch = pitch}, true);
    }

    SoundId AudioSystem::playOneShot(std::string_view uri, float volume, float pitch)
    {
        if (!m_Impl->engineValid || !m_Assets)
            return kInvalidSoundId;
        auto handle = m_Assets->loadAudioSync(uri);
        auto* clip  = m_Impl->ensureClip(handle);
        return m_Impl->spawnSound(
            clip, handle.uuid(), {.kind = Impl::SoundKind::eOneShot, .volume = volume, .pitch = pitch}, true);
    }

    SoundId AudioSystem::playOneShotAt(const CoreUUID& uuid, const glm::vec3& position, float volume, float pitch)
    {
        if (!m_Impl->engineValid || !m_Assets || !uuid.valid())
            return kInvalidSoundId;
        auto* clip = m_Impl->ensureClip(m_Assets->loadAudioSync(uuid));
        const SoundId id = m_Impl->spawnSound(
            clip, uuid, {.kind = Impl::SoundKind::eOneShot, .spatial = true, .volume = volume, .pitch = pitch}, false);
        if (auto* active = m_Impl->find(id))
        {
            ma_sound_set_position(active->sound.get(), position.x, position.y, position.z);
            ma_sound_start(active->sound.get());
        }
        return id;
    }

    SoundId AudioSystem::playOneShotAt(std::string_view uri, const glm::vec3& position, float volume, float pitch)
    {
        if (!m_Impl->engineValid || !m_Assets)
            return kInvalidSoundId;
        auto handle = m_Assets->loadAudioSync(uri);
        if (!handle.ready())
            return kInvalidSoundId;
        return playOneShotAt(handle.uuid(), position, volume, pitch);
    }

    SoundId AudioSystem::playMusic(const CoreUUID& uuid, const AudioPlayParams& params)
    {
        if (!m_Impl->engineValid || !m_Assets || !uuid.valid())
            return kInvalidSoundId;

        // Single music slot: fade the previous track out over the new track's fade-in.
        if (m_Impl->musicId != kInvalidSoundId)
            stopMusic(params.fadeInMs);

        auto* clip = m_Impl->ensureClip(m_Assets->loadAudioSync(uuid));
        const SoundId id = m_Impl->spawnSound(clip,
                                              uuid,
                                              {.kind     = Impl::SoundKind::eMusic,
                                               .volume   = params.volume,
                                               .pitch    = params.pitch,
                                               .loop     = params.loop,
                                               .fadeInMs = params.fadeInMs},
                                              true);
        m_Impl->musicId = id;
        return id;
    }

    SoundId AudioSystem::playMusic(std::string_view uri, const AudioPlayParams& params)
    {
        if (!m_Impl->engineValid || !m_Assets)
            return kInvalidSoundId;
        auto handle = m_Assets->loadAudioSync(uri);
        if (!handle.ready())
            return kInvalidSoundId;
        return playMusic(handle.uuid(), params);
    }

    void AudioSystem::stopMusic(float fadeOutMs)
    {
        if (m_Impl->musicId == kInvalidSoundId)
            return;
        stop(m_Impl->musicId, fadeOutMs);
        m_Impl->musicId = kInvalidSoundId;
    }

    void AudioSystem::stop(SoundId id, float fadeOutMs)
    {
        auto* active = m_Impl->find(id);
        if (!active)
            return;
        if (fadeOutMs > 0.0f && ma_sound_is_playing(active->sound.get()))
        {
            ma_sound_stop_with_fade_in_milliseconds(active->sound.get(), static_cast<ma_uint64>(fadeOutMs));
            active->fadingOut = true; // recycled in onUpdate once the fade lands
        }
        else
        {
            m_Impl->destroySound(id);
        }
    }

    void AudioSystem::pause(SoundId id)
    {
        if (auto* active = m_Impl->find(id))
            ma_sound_stop(active->sound.get());
    }

    void AudioSystem::resume(SoundId id)
    {
        if (auto* active = m_Impl->find(id))
            ma_sound_start(active->sound.get());
    }

    void AudioSystem::setVolume(SoundId id, float volume)
    {
        if (auto* active = m_Impl->find(id))
            ma_sound_set_volume(active->sound.get(), std::max(volume, 0.0f));
    }

    void AudioSystem::setPitch(SoundId id, float pitch)
    {
        if (auto* active = m_Impl->find(id))
            ma_sound_set_pitch(active->sound.get(), std::max(pitch, 0.01f));
    }

    void AudioSystem::setLooping(SoundId id, bool loop)
    {
        if (auto* active = m_Impl->find(id))
            ma_sound_set_looping(active->sound.get(), loop ? MA_TRUE : MA_FALSE);
    }

    bool AudioSystem::isPlaying(SoundId id) const
    {
        const auto* active = m_Impl->find(id);
        return active && active->sound && ma_sound_is_playing(active->sound.get()) == MA_TRUE;
    }

    // ------------------------------------------------------------
    // ECS component control
    // ------------------------------------------------------------

    bool AudioSystem::play(entt::entity entity, bool restart)
    {
        if (!m_Worlds)
            return false;
        auto* source = m_Worlds->world().registry().try_get<AudioSourceComponent>(entity);
        if (!source)
            return false;
        source->playing     = true;
        source->playOnStart = false;
        if (restart)
        {
            if (auto it = m_Impl->entitySounds.find(entity); it != m_Impl->entitySounds.end())
            {
                if (auto* active = m_Impl->find(it->second.id))
                    ma_sound_seek_to_pcm_frame(active->sound.get(), 0);
            }
        }
        return true;
    }

    bool AudioSystem::pause(entt::entity entity)
    {
        if (!m_Worlds)
            return false;
        auto* source = m_Worlds->world().registry().try_get<AudioSourceComponent>(entity);
        if (!source)
            return false;
        source->playing     = false;
        source->playOnStart = false;
        return true;
    }

    bool AudioSystem::stop(entt::entity entity)
    {
        if (!m_Worlds)
            return false;
        auto* source = m_Worlds->world().registry().try_get<AudioSourceComponent>(entity);
        if (!source)
            return false;
        source->playing     = false;
        source->playOnStart = false;
        if (auto it = m_Impl->entitySounds.find(entity); it != m_Impl->entitySounds.end())
        {
            if (auto* active = m_Impl->find(it->second.id))
            {
                ma_sound_stop(active->sound.get());
                ma_sound_seek_to_pcm_frame(active->sound.get(), 0);
            }
        }
        return true;
    }

    // ------------------------------------------------------------
    // Global
    // ------------------------------------------------------------

    void AudioSystem::setMasterVolume(float volume)
    {
        m_Impl->masterVolume = std::max(volume, 0.0f);
        if (m_Impl->engineValid)
            ma_engine_set_volume(&m_Impl->engine, m_Impl->masterVolume);
    }

    float AudioSystem::masterVolume() const { return m_Impl->masterVolume; }

    void AudioSystem::setPlaybackState(bool playing, bool paused)
    {
        if (!playing)
            paused = false;

        m_PlaybackPlaying = playing;
        m_PlaybackPaused  = paused;

        if (!m_Impl->engineValid)
            return;

        if (!playing)
        {
            // Editor stop: drop every live instance; component sounds are re-seeded on
            // the next play-mode entry.
            for (auto& [id, active] : m_Impl->sounds)
            {
                if (active.sound)
                {
                    ma_sound_stop(active.sound.get());
                    ma_sound_uninit(active.sound.get());
                }
            }
            m_Impl->sounds.clear();
            m_Impl->entitySounds.clear();
            m_Impl->musicId = kInvalidSoundId;
            return;
        }

        if (paused)
        {
            for (auto& [id, active] : m_Impl->sounds)
            {
                if (active.sound && ma_sound_is_playing(active.sound.get()))
                {
                    ma_sound_stop(active.sound.get());
                    active.pausedByPlayback = true;
                }
            }
        }
        else
        {
            for (auto& [id, active] : m_Impl->sounds)
            {
                if (active.sound && active.pausedByPlayback)
                {
                    ma_sound_start(active.sound.get());
                    active.pausedByPlayback = false;
                }
            }
        }
    }

    bool AudioSystem::backendReady() const { return m_Impl->engineValid && m_Impl->deviceStarted; }

    size_t AudioSystem::debugActiveSoundCount() const { return m_Impl->sounds.size(); }

    // ------------------------------------------------------------
    // Frame updates
    // ------------------------------------------------------------

    void AudioSystem::onUpdate(fsec)
    {
        if (!m_Impl->engineValid)
            return;

        // Recycle finished one-shots and fade-out stopped sounds. Component sounds are
        // reconciled in onPreRender and survive until their entity/component goes away.
        for (auto it = m_Impl->sounds.begin(); it != m_Impl->sounds.end();)
        {
            auto& active   = it->second;
            bool  finished = false;
            if (active.sound)
            {
                if (active.fadingOut && ma_sound_is_playing(active.sound.get()) == MA_FALSE)
                    finished = true;
                else if (active.kind == Impl::SoundKind::eOneShot && ma_sound_at_end(active.sound.get()) == MA_TRUE)
                    finished = true;
            }
            else
            {
                finished = true;
            }

            if (finished)
            {
                if (active.sound)
                {
                    ma_sound_stop(active.sound.get());
                    ma_sound_uninit(active.sound.get());
                }
                if (m_Impl->musicId == it->first)
                    m_Impl->musicId = kInvalidSoundId;
                it = m_Impl->sounds.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void AudioSystem::onPreRender()
    {
        if (!m_Impl->engineValid || !m_Worlds)
            return;

        auto& registry = m_Worlds->world().registry();

        // --- listener pose ---
        // Prefer an explicit primary AudioListenerComponent; otherwise fall back to the active
        // camera so spatial audio "just works" against the default view (mirrors the common
        // engine convention of an audio listener riding the main camera).
        const auto applyListener = [&](const glm::mat4& world) {
            const glm::vec3 position {world[3]};
            const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
            const glm::vec3 up      = glm::normalize(glm::vec3(world[1]));
            ma_engine_listener_set_position(&m_Impl->engine, 0, position.x, position.y, position.z);
            ma_engine_listener_set_direction(&m_Impl->engine, 0, forward.x, forward.y, forward.z);
            ma_engine_listener_set_world_up(&m_Impl->engine, 0, up.x, up.y, up.z);
        };

        bool listenerSet = false;
        for (auto [entity, listener, transform] :
             registry.view<AudioListenerComponent, TransformComponent>().each())
        {
            if (!listener.primary)
                continue;
            applyListener(transform.worldMatrix);
            listenerSet = true;
            break;
        }
        if (!listenerSet && m_Cameras)
        {
            // RenderCamera::inverseView is the camera's world transform (forward = -Z column).
            for (const auto& camera : m_Cameras->cameras())
            {
                if (camera.isXRView && !camera.isXRPrimaryView)
                    continue;
                applyListener(camera.inverseView);
                listenerSet = true;
                break;
            }
        }

        const bool playbackActive = m_PlaybackPlaying && !m_PlaybackPaused;

        // --- sources ---
        for (auto [entity, source] : registry.view<AudioSourceComponent>().each())
        {
            auto& state = m_Impl->entitySounds[entity];

            if (!state.seeded && playbackActive)
            {
                if (source.playOnStart)
                    source.playing = true;
                state.seeded = true;
            }

            if (!playbackActive)
                continue;

            // Spawn lazily on the first frame the source wants to play.
            if (state.id == kInvalidSoundId && source.playing && source.clip.valid() && m_Assets)
            {
                auto* clip = m_Impl->ensureClip(m_Assets->loadAudioSync(source.clip));
                state.id   = m_Impl->spawnSound(clip,
                                              source.clip,
                                              {.kind    = Impl::SoundKind::eComponent,
                                               .spatial = source.spatial,
                                               .volume  = source.volume,
                                               .pitch   = source.pitch,
                                               .loop    = source.loop,
                                               .owner   = entity},
                                              false);
                if (state.id != kInvalidSoundId)
                {
                    state.volume      = source.volume;
                    state.pitch       = source.pitch;
                    state.loop        = source.loop;
                    state.spatial     = source.spatial;
                    state.minDistance = source.minDistance;
                    state.maxDistance = source.maxDistance;
                    state.rolloff     = -1.0f; // force the initial spatial-parameter apply below
                }
            }

            auto* active = m_Impl->find(state.id);
            if (!active)
                continue;
            ma_sound* sound = active->sound.get();

            // Dirty-check live component edits.
            if (source.volume != state.volume)
            {
                ma_sound_set_volume(sound, std::max(source.volume, 0.0f));
                state.volume = source.volume;
            }
            if (source.pitch != state.pitch)
            {
                ma_sound_set_pitch(sound, std::max(source.pitch, 0.01f));
                state.pitch = source.pitch;
            }
            if (source.loop != state.loop)
            {
                ma_sound_set_looping(sound, source.loop ? MA_TRUE : MA_FALSE);
                state.loop = source.loop;
            }
            if (source.spatial != state.spatial)
            {
                ma_sound_set_spatialization_enabled(sound, source.spatial ? MA_TRUE : MA_FALSE);
                state.spatial = source.spatial;
                state.rolloff = -1.0f;
            }
            if (source.spatial && (source.minDistance != state.minDistance ||
                                   source.maxDistance != state.maxDistance || source.rolloff != state.rolloff))
            {
                ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
                ma_sound_set_min_distance(sound, std::max(source.minDistance, 0.001f));
                ma_sound_set_max_distance(sound, std::max(source.maxDistance, source.minDistance));
                ma_sound_set_rolloff(sound, std::max(source.rolloff, 0.0f));
                state.minDistance = source.minDistance;
                state.maxDistance = source.maxDistance;
                state.rolloff     = source.rolloff;
            }

            if (source.spatial)
            {
                if (const auto* transform = registry.try_get<TransformComponent>(entity))
                {
                    const glm::vec3 position {transform->worldMatrix[3]};
                    ma_sound_set_position(sound, position.x, position.y, position.z);
                }
            }

            const bool soundPlaying = ma_sound_is_playing(sound) == MA_TRUE;
            const bool atEnd        = ma_sound_at_end(sound) == MA_TRUE;
            if (source.playing)
            {
                if (atEnd && !source.loop)
                {
                    // Finished naturally: reflect it in the component (gameplay can set
                    // playing=true again to restart from the top).
                    source.playing = false;
                    ma_sound_seek_to_pcm_frame(sound, 0);
                }
                else if (!soundPlaying)
                {
                    if (atEnd)
                        ma_sound_seek_to_pcm_frame(sound, 0);
                    ma_sound_start(sound);
                }
            }
            else if (soundPlaying)
            {
                ma_sound_stop(sound);
            }
        }

        // --- sweep entries whose entity or component is gone (entity destroyed -> sound stops) ---
        for (auto it = m_Impl->entitySounds.begin(); it != m_Impl->entitySounds.end();)
        {
            const entt::entity entity = it->first;
            if (!registry.valid(entity) || !registry.all_of<AudioSourceComponent>(entity))
            {
                m_Impl->destroySound(it->second.id);
                it = m_Impl->entitySounds.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
} // namespace vultra
