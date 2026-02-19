#include "vultra/function/world/world_system.hpp"
#include "vultra/core/engine/engine_context.hpp"

namespace vultra
{
    bool WorldSystem::onInit()
    {
        m_World = std::make_unique<World>();

        ctx().services.provide<IWorldService>(this);
        return true;
    }

    void WorldSystem::onShutdown() { m_World.reset(); }
} // namespace vultra
