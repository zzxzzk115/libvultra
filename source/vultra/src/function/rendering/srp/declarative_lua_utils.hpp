#pragma once

// Internal (src-level) header: small sol::table accessors shared by the
// declarative renderer runtime (declarative_renderer.cpp) and its Lua asset
// loader (declarative_renderer_asset_loader.cpp). Not part of the public API.

#include <sol/sol.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace vultra
{
    [[nodiscard]] inline std::string getString(sol::table table, const char* key, std::string fallback = {})
    {
        sol::object value = table[key];
        return value.is<std::string>() ? value.as<std::string>() : std::move(fallback);
    }

    [[nodiscard]] inline int getInt(sol::table table, const char* key, int fallback = 0)
    {
        sol::object value = table[key];
        return value.is<int>() ? value.as<int>() : fallback;
    }

    [[nodiscard]] inline bool getBool(sol::table table, const char* key, bool fallback = false)
    {
        sol::object value = table[key];
        return value.is<bool>() ? value.as<bool>() : fallback;
    }

    [[nodiscard]] inline std::vector<std::string> getStringList(sol::table table, const char* key)
    {
        std::vector<std::string> out;
        sol::object              value = table[key];
        if (value.is<std::string>())
            out.push_back(value.as<std::string>());
        else if (value.is<sol::table>())
        {
            sol::table values = value.as<sol::table>();
            for (const auto& [_, item] : values)
            {
                static_cast<void>(_);
                if (item.is<std::string>())
                    out.push_back(item.as<std::string>());
            }
        }

        out.erase(std::remove_if(out.begin(), out.end(), [](const auto& item) { return item.empty(); }), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
} // namespace vultra
