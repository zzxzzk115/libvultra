#pragma once

#include <vbase/service/service_registry.hpp>

#include <cstdint>

namespace vultra
{
    class IPhysicsService
    {
    public:
        SERVICE_REGISTER(IPhysicsService)

        virtual ~IPhysicsService() = default;

        virtual void setEnabled(bool enabled) = 0;
        virtual bool enabled() const = 0;

        virtual void setPlaybackState(bool playing, bool paused) = 0;
        virtual bool playing() const = 0;
        virtual bool paused() const = 0;
        virtual void requestSingleStep() = 0;

        virtual void setFixedTimeStep(float seconds) = 0;
        virtual float fixedTimeStep() const = 0;

        virtual uint32_t bodyCount() const = 0;
    };
} // namespace vultra
