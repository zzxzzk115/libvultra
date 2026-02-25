#pragma once

#include <entt/entt.hpp>

namespace vultra
{
    // ---------------------------------------------------------------------
    // World (Logic)
    //
    // - Runtime logical world state.
    // - Owns an EnTT registry.
    // - MUST NOT contain scene asset parsing/loading logic.
    // ---------------------------------------------------------------------
    class World
    {
    public:
        World()  = default;
        ~World() = default;

        World(const World&)            = delete;
        World& operator=(const World&) = delete;
        World(World&&)                 = default;
        World& operator=(World&&)      = default;

        entt::registry&       registry() { return m_Registry; }
        const entt::registry& registry() const { return m_Registry; }

        void clear() { m_Registry.clear(); }

        entt::entity createEntity() { return m_Registry.create(); }
        void         destroyEntity(entt::entity e)
        {
            if (e != entt::null && m_Registry.valid(e))
            {
                m_Registry.destroy(e);
            }
        }

    private:
        entt::registry m_Registry;
    };
} // namespace vultra
