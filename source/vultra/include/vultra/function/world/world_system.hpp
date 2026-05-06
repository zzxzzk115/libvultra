#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/world.hpp"

#include <memory>

namespace vultra
{
    class WorldSystem final : public EngineSubsystem, public IWorldService
    {
    public:
        ENGINE_SUBSYSTEM(WorldSystem)

        WorldSystem()           = default;
        ~WorldSystem() override = default;

        bool onInit() override;
        void onShutdown() override;

        void onPreRender() override;

        // IWorldService
        World& world() override { return *m_World; }

    private:
        std::unique_ptr<World> m_World;

        void updateWorldTransforms();
    };
} // namespace vultra
