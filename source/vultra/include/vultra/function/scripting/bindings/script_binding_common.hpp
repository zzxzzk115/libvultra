#pragma once

#include "vultra/core/base/common_context.hpp"

#include <magic_enum/magic_enum.hpp>
#include <sol/sol.hpp>

#include <cctype>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>

namespace vultra::script_binding
{
    // Logs a one-time deprecation warning per old name (doc/lua_api_design.md
    // section 7). Use for usertype members that cannot go through the Lua-side
    // alias shim in script_deprecations.cpp.
    inline void warnDeprecated(std::string_view oldName, std::string_view newName)
    {
        static std::set<std::string, std::less<>> warned;
        if (warned.emplace(oldName).second)
            VULTRA_CORE_WARN("[Lua] '{}' is deprecated, use '{}'", oldName, newName);
    }

    inline sol::table getOrCreateTable(sol::state& lua, std::string_view name)
    {
        sol::object obj = lua[name.data()];
        if (obj.valid() && obj.get_type() == sol::type::table)
            return obj.as<sol::table>();
        auto created     = lua.create_table();
        lua[name.data()] = created;
        return created;
    }

    inline std::string normalizeEnumName(std::string_view rawName)
    {
        if (rawName.size() >= 2 && rawName[0] == 'e' && std::isupper(static_cast<unsigned char>(rawName[1])) != 0)
            return std::string(rawName.substr(1));
        return std::string(rawName);
    }

    template<typename Getter, typename Setter>
    auto property(Getter&& getter, Setter&& setter)
    {
        return sol::property(std::forward<Getter>(getter), std::forward<Setter>(setter));
    }

    template<typename Getter>
    auto readonlyProperty(Getter&& getter)
    {
        return sol::property(std::forward<Getter>(getter));
    }

    template<typename Enum>
    void bindEnumTable(sol::state& lua, std::string_view tableName)
    {
        static_assert(std::is_enum_v<Enum>, "Enum type required");

        auto table = getOrCreateTable(lua, tableName);
        for (Enum value : magic_enum::enum_values<Enum>())
        {
            const auto name = magic_enum::enum_name(value);
            if (!name.empty())
                table[normalizeEnumName(name)] = static_cast<int>(value);
        }
    }
} // namespace vultra::script_binding

#define VULTRA_LUA_PROPERTY(getter, setter) ::vultra::script_binding::property((getter), (setter))
#define VULTRA_LUA_READONLY_PROPERTY(getter) ::vultra::script_binding::readonlyProperty((getter))
