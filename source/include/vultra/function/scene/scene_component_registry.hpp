#pragma once

#include <entt/entt.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    class SceneComponentRegistry
    {
    public:
        struct Entry
        {
            std::string     name;
            entt::id_type   nameId {0};
            entt::meta_type meta;

            std::vector<std::string> fields; // stable field names for .vscn writer

            std::function<void*(entt::registry&, entt::entity)> emplaceDefault;
            std::function<void*(entt::registry&, entt::entity)> getPtr;
            std::function<bool(entt::registry&, entt::entity)>  has;
        };

        template<typename T>
        void registerComponent(const std::string& name, std::vector<std::string> fields = {})
        {
            using namespace entt::literals;
            Entry e;
            e.name   = name;
            e.nameId = entt::hashed_string {name.c_str()}.value();
            e.meta   = entt::resolve(e.nameId);
            e.fields = std::move(fields);

            e.emplaceDefault = [](entt::registry& r, entt::entity ent) -> void* {
                if (r.all_of<T>(ent))
                    return static_cast<void*>(&r.get<T>(ent));
                return static_cast<void*>(&r.emplace<T>(ent));
            };
            e.getPtr = [](entt::registry& r, entt::entity ent) -> void* {
                return r.all_of<T>(ent) ? static_cast<void*>(&r.get<T>(ent)) : nullptr;
            };
            e.has = [](entt::registry& r, entt::entity ent) -> bool { return r.all_of<T>(ent); };

            m_NameToEntry[name]  = e;
            m_IdToName[e.nameId] = name;
            m_Entries.push_back(e);
        }

        const Entry* find(std::string_view name) const
        {
            auto it = m_NameToEntry.find(std::string(name));
            return it != m_NameToEntry.end() ? &it->second : nullptr;
        }

        const Entry* find(entt::id_type nameId) const
        {
            auto itn = m_IdToName.find(nameId);
            if (itn == m_IdToName.end())
                return nullptr;
            return find(itn->second);
        }

        const std::vector<Entry>& entries() const { return m_Entries; }

    private:
        std::unordered_map<std::string, Entry>         m_NameToEntry;
        std::unordered_map<entt::id_type, std::string> m_IdToName;
        std::vector<Entry>                             m_Entries;
    };
} // namespace vultra
