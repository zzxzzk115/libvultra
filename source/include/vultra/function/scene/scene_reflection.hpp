#pragma once

#include <entt/entt.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace vultra
{
    // Bridge between .vscn strings and EnTT registry component storage.
    class SceneComponentRegistry
    {
    public:
        using EmplaceFn = void (*)(entt::registry&, entt::entity);
        using GetPtrFn  = void* (*)(entt::registry&, entt::entity);

        struct Entry
        {
            std::string   name;
            entt::id_type typeId = 0; // entt::meta_type::id()
            EmplaceFn     emplaceDefault {};
            GetPtrFn      getPtr {};
        };

        template<typename T>
        void registerComponent(std::string_view name);

        const Entry* findByName(std::string_view name) const;
        const Entry* findByType(entt::id_type typeId) const;

    private:
        std::unordered_map<std::string, Entry>         m_ByName;
        std::unordered_map<entt::id_type, std::string> m_TypeToName;
    };

    template<typename T>
    void SceneComponentRegistry::registerComponent(std::string_view name)
    {
        Entry e;
        e.name   = std::string(name);
        e.typeId = entt::resolve<T>().id();

        e.emplaceDefault = [](entt::registry& r, entt::entity ent) {
            if (!r.any_of<T>(ent))
            {
                r.emplace<T>(ent);
            }
        };

        e.getPtr = [](entt::registry& r, entt::entity ent) -> void* {
            return r.any_of<T>(ent) ? static_cast<void*>(&r.get<T>(ent)) : nullptr;
        };

        m_TypeToName[e.typeId] = e.name;
        m_ByName[e.name]       = std::move(e);
    }

    // Registers meta types + fields for built-in components used in scenes.
    void registerSceneComponentMeta();

} // namespace vultra
