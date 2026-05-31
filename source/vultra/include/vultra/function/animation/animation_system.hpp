#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/core/base/uuid.hpp"
#include "vultra/function/asset/asset_handle.hpp"
#include "vultra/function/resource/cpu_asset.hpp"
#include "vultra/function/services/animation_service.hpp"

#include <vasset/vanimation.hpp>

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>

#include <memory>
#include <unordered_map>

namespace vultra
{
    class IAssetService;
    class IWorldService;

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

        const ozz::animation::Skeleton*  runtimeSkeleton(const CoreUUID& uuid);
        const ozz::animation::Animation* runtimeAnimation(const CoreUUID& uuid);

    private:
        IAssetService* m_Assets {nullptr};
        IWorldService* m_Worlds {nullptr};

        bool     m_PlaybackPlaying {true};
        bool     m_PlaybackPaused {false};
        uint32_t m_SingleStepRequests {0u};

        std::unordered_map<CoreUUID, SkeletonRuntime>  m_Skeletons;
        std::unordered_map<CoreUUID, AnimationRuntime> m_Animations;
    };
} // namespace vultra
