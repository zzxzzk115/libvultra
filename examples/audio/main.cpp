// Audio demo: a looping spatial source (resources/sounds/coin.wav) orbits the listener
// so panning/attenuation are audible. Left click fires a 2D one-shot, M toggles the
// music slot (fade in/out), S stops it. On web, audio starts after the first
// click/keypress (browser gesture unlock).
#include <vultra/core/app/demo_app_entry.hpp>
#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/core/engine/engine_subsystem.hpp>
#include <vultra/core/services/input_service.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/audio_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/audio_listener_component.hpp>
#include <vultra/function/world/components/audio_source_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <vasset/vasset_type.hpp>

#include <cmath>

using namespace vultra;

namespace
{
    class AudioDemoSystem final : public EngineSubsystem
    {
    public:
        ENGINE_SUBSYSTEM(AudioDemoSystem)

        void onUpdate(fsec dt) override
        {
            auto* worlds = ctx().services.tryGet<IWorldService>();
            auto* audio  = ctx().services.tryGet<IAudioService>();
            auto* assets = ctx().services.tryGet<IAssetService>();
            auto* input  = ctx().services.tryGet<IInputService>();
            if (!worlds || !audio || !assets)
                return;

            if (!m_SceneReady)
            {
                setupScene(*worlds, *assets);
                m_SceneReady = true;
            }

            // Orbit the spatial source around the listener at the origin.
            m_OrbitAngle += dt.count() * 0.8f;
            auto& registry = worlds->world().registry();
            if (registry.valid(m_Source))
            {
                auto& transform = registry.get<TransformComponent>(m_Source);
                transform.position = {std::cos(m_OrbitAngle) * 5.0f, 0.0f, std::sin(m_OrbitAngle) * 5.0f};
                transform.dirty    = true;
            }

            if (!input || !m_Clip.valid())
                return;

            if (input->isMouseButtonPressed(MouseCode::eLeft))
                audio->playOneShot(m_Clip, 0.8f, 1.5f);

            if (input->isKeyPressed(KeyCode::eM))
            {
                if (m_Music != kInvalidSoundId && audio->isPlaying(m_Music))
                {
                    audio->stopMusic(500.0f);
                    m_Music = kInvalidSoundId;
                }
                else
                {
                    m_Music = audio->playMusic(m_Clip, {.volume = 0.5f, .loop = true, .fadeInMs = 500.0f});
                }
            }
            if (input->isKeyPressed(KeyCode::eS))
            {
                audio->stopMusic(500.0f);
                m_Music = kInvalidSoundId;
            }
        }

    private:
        void setupScene(IWorldService& worlds, IAssetService& assets)
        {
            // Resolve the demo clip cooked from resources/sounds/coin.wav.
            for (const auto& [key, entry] : assets.registry().getRegistry())
            {
                if (entry.type == vasset::VAssetType::eAudio &&
                    entry.sourcePath.find("coin") != std::string::npos)
                {
                    vbase::UUID parsed {};
                    if (vbase::try_parse_uuid(key.c_str(), parsed))
                        m_Clip = CoreUUID {parsed};
                    break;
                }
            }
            if (!m_Clip.valid())
            {
                VULTRA_CLIENT_WARN("[AudioDemo] no audio asset found (expected resources/sounds/coin.wav)");
                return;
            }

            auto& world    = worlds.world();
            auto& registry = world.registry();

            auto listener = world.createEntity();
            registry.get_or_emplace<TransformComponent>(listener).dirty = true;
            registry.emplace<AudioListenerComponent>(listener);

            m_Source = world.createEntity();
            auto& transform = registry.get_or_emplace<TransformComponent>(m_Source);
            transform.position = {5.0f, 0.0f, 0.0f};
            transform.dirty    = true;
            registry.emplace<AudioSourceComponent>(m_Source,
                                                   AudioSourceComponent {
                                                       .clip        = m_Clip,
                                                       .loop        = true,
                                                       .minDistance = 1.0f,
                                                       .maxDistance = 30.0f,
                                                   });

            VULTRA_CLIENT_INFO("[AudioDemo] orbiting spatial source ready. "
                               "Left click: one-shot | M: toggle music (fade) | S: stop music");
        }

        bool         m_SceneReady {false};
        float        m_OrbitAngle {0.0f};
        CoreUUID     m_Clip {};
        entt::entity m_Source {entt::null};
        SoundId      m_Music {kInvalidSoundId};
    };

    class AudioDemoApp final : public DemoAppHost
    {
    protected:
        std::string_view demoWindowTitle() const override { return "Vultra Audio Demo"; }
        bool             demoEnableExperimentalWebGPUContent() const override { return true; }
        void             onConfigureDemo(Engine& engine) override { engine.emplaceSubsystem<AudioDemoSystem>(); }
    };
} // namespace

VULTRA_DEMO_APP_MAIN(AudioDemoApp)
