#pragma once

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/base/uuid.hpp"
#include "vultra/function/scenegraph/components.hpp"
#include "vultra/function/scenegraph/logic_scene.hpp"

#include <entt/entt.hpp>

namespace vultra
{
    class Entity
    {
    public:
        Entity() = default;
        Entity(entt::entity handle, LogicScene* scene) : m_EntityHandle(handle), m_Scene(scene) {}
        Entity(const Entity& other) = default;

        template<typename T, typename... Args>
        T& addComponent(Args&&... args)
        {
            VULTRA_CORE_ASSERT(!hasComponent<T>(), "[Entity] Can't add a duplicate component!");
            T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
            m_Scene->onComponentAdded<T>(*this, component);
            return component;
        }

        template<typename T, typename... Args>
        T& addOrReplaceComponent(Args&&... args)
        {
            T& component = m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
            m_Scene->onComponentAdded<T>(*this, component);
            return component;
        }

        template<typename T>
        T& getComponent()
        {
            VULTRA_CORE_ASSERT(hasComponent<T>(), "[Entity] Entity does not have any components!");
            return m_Scene->m_Registry.get<T>(m_EntityHandle);
        }

        template<typename T>
        bool hasComponent()
        {
            return m_Scene->m_Registry.any_of<T>(m_EntityHandle);
        }

        template<typename T>
        void removeComponent()
        {
            VULTRA_CORE_ASSERT(hasComponent<T>(), "[Entity] Entity does not have any components!");
            m_Scene->m_Registry.remove<T>(m_EntityHandle);
        }

        operator bool() const { return m_EntityHandle != entt::null; }

        operator entt::entity() const { return m_EntityHandle; }

        operator uint32_t() const { return static_cast<uint32_t>(m_EntityHandle); }

        // helper functions (getters)
        CoreUUID getCoreUUID() { return getComponent<IDComponent>().id; }

        // TODO: helper functions (setters)

        bool operator==(const Entity& other) const
        {
            return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
        }

        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        entt::entity m_EntityHandle = entt::null;
        LogicScene*  m_Scene        = nullptr;
    };
} // namespace vultra