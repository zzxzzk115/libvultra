#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/core/base/uuid.hpp"
#include "vultra/function/animation/animator_graph.hpp"
#include "vultra/function/asset/asset_handle.hpp"
#include "vultra/function/resource/cpu_asset.hpp"
#include "vultra/function/services/animation_service.hpp"

#include <vasset/vanimation.hpp>

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace vultra
{
    class IAssetService;
    class IWorldService;
    class ITimingService;
    struct AnimatorComponent;

    class AnimationSystem final : public EngineSubsystem, public IAnimationService
    {
    public:
        ENGINE_SUBSYSTEM(AnimationSystem)

        bool onInit() override;
        void onShutdown() override;
        void onUpdate(fsec dt) override;

        void setPlaybackState(bool playing, bool paused) override;
        bool playing() const override { return m_PlaybackPlaying; }
        bool paused() const override { return m_PlaybackPaused; }
        void requestSingleStep() override;
        void updateWorld(World& world, fsec dt) override;

        bool play(entt::entity entity, bool restart = false) override;
        bool pause(entt::entity entity) override;
        bool stop(entt::entity entity) override;
        bool setAnimation(entt::entity entity, const CoreUUID& animation, bool restart = true) override;
        bool setTime(entt::entity entity, float seconds) override;
        bool setNormalizedTime(entt::entity entity, float normalizedTime) override;
        bool setSpeed(entt::entity entity, float speed) override;
        bool setLoop(entt::entity entity, bool loop) override;
        AnimatorPlaybackState playbackState(entt::entity entity) override;
        uint32_t jointCount(const CoreUUID& skeleton) override;
        float animationDuration(const CoreUUID& animation) override;

        bool                    setFloat(entt::entity entity, const std::string& name, float value) override;
        bool                    setBool(entt::entity entity, const std::string& name, bool value) override;
        bool                    setTrigger(entt::entity entity, const std::string& name) override;
        float                   getFloat(entt::entity entity, const std::string& name) override;
        bool                    getBool(entt::entity entity, const std::string& name) override;
        AnimatorControllerState controllerState(entt::entity entity) override;

    private:
        struct SkeletonRuntime
        {
            AssetHandle<vasset::VSkeleton, resource::CpuAsset> handle;
            std::unique_ptr<ozz::animation::Skeleton>          skeleton;
        };

        struct AnimationRuntime
        {
            AssetHandle<vasset::VAnimation, resource::CpuAsset> handle;
            std::unique_ptr<ozz::animation::Animation>          animation;
        };

        // Per-entity animator-graph state-machine runtime (not serialized).
        struct ControllerRuntime
        {
            std::string                            graphUri;
            animator_graph::Graph                  graph;
            bool                                   loaded {false};
            std::unordered_map<std::string, float> floats;
            std::unordered_map<std::string, bool>  bools;
            std::unordered_set<std::string>        triggers;
            int                                    currentState {-1};
            float                                  currentTime {0.0f};
            int                                    targetState {-1};
            float                                  targetTime {0.0f};
            float                                  transitionTime {0.0f};
            float                                  transitionDuration {0.0f};
        };

        const ozz::animation::Skeleton*  runtimeSkeleton(const CoreUUID& uuid);
        const ozz::animation::Animation* runtimeAnimation(const CoreUUID& uuid);

        ControllerRuntime* ensureController(entt::entity entity);
        void updateGraphAnimator(World& world, entt::entity entity, AnimatorComponent& animator, fsec dt);
        bool conditionMet(ControllerRuntime& rt, const animator_graph::Condition& c) const;
        bool transitionReady(ControllerRuntime& rt, const animator_graph::Transition& t, float currentNormalized) const;

    private:
        IAssetService* m_Assets {nullptr};
        IWorldService* m_Worlds {nullptr};
        ITimingService* m_Timing {nullptr};

        bool     m_PlaybackPlaying {true};
        bool     m_PlaybackPaused {false};
        uint32_t m_SingleStepRequests {0u};

        std::unordered_map<CoreUUID, SkeletonRuntime>  m_Skeletons;
        std::unordered_map<CoreUUID, AnimationRuntime> m_Animations;
        std::unordered_map<entt::entity, ControllerRuntime> m_Controllers;
    };
} // namespace vultra
