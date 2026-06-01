#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/uuid.hpp"

#include <vbase/service/service_registry.hpp>

#include <entt/entity/fwd.hpp>

#include <cstdint>
#include <optional>

namespace vultra
{
    class World;

    struct AnimatorPlaybackState
    {
        bool  valid {false};
        bool  playing {false};
        bool  loop {true};
        float speed {1.0f};
        float time {0.0f};
        float duration {0.0f};
        float normalizedTime {0.0f};
        CoreUUID skeleton;
        CoreUUID animation;
    };

    class IAnimationService
    {
    public:
        SERVICE_REGISTER(IAnimationService)

        virtual ~IAnimationService() = default;

        virtual void setPlaybackState(bool playing, bool paused) = 0;
        virtual bool playing() const = 0;
        virtual bool paused() const = 0;
        virtual void requestSingleStep() = 0;
        virtual void updateWorld(World& world, fsec dt) = 0;

        virtual bool play(entt::entity entity, bool restart = false) = 0;
        virtual bool pause(entt::entity entity) = 0;
        virtual bool stop(entt::entity entity) = 0;
        virtual bool setAnimation(entt::entity entity, const CoreUUID& animation, bool restart = true) = 0;
        virtual bool setTime(entt::entity entity, float seconds) = 0;
        virtual bool setNormalizedTime(entt::entity entity, float normalizedTime) = 0;
        virtual bool setSpeed(entt::entity entity, float speed) = 0;
        virtual bool setLoop(entt::entity entity, bool loop) = 0;
        virtual AnimatorPlaybackState playbackState(entt::entity entity) = 0;
        virtual uint32_t jointCount(const CoreUUID& skeleton) = 0;
        virtual float animationDuration(const CoreUUID& animation) = 0;
    };
} // namespace vultra
