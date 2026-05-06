#pragma once

#include <vbase/service/service_registry.hpp>

namespace vultra
{
    class World;

    class IWorldService
    {
    public:
        SERVICE_REGISTER(IWorldService)

        virtual World& world() = 0;
    };
} // namespace vultra
