#pragma once

#include <vbase/service/service_registry.hpp>

namespace vultra
{
    class IAnimationService
    {
    public:
        SERVICE_REGISTER(IAnimationService)

        virtual ~IAnimationService() = default;

        virtual void setPlaybackState(bool playing, bool paused) = 0;
        virtual bool playing() const = 0;
        virtual bool paused() const = 0;
        virtual void requestSingleStep() = 0;
    };
} // namespace vultra
