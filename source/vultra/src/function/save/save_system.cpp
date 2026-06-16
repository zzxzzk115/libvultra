#include "vultra/function/save/save_system.hpp"

#include "vultra/core/base/common_context.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace vultra
{
    namespace
    {
        std::string envValue(const char* name)
        {
            const char* value = std::getenv(name);
            return value != nullptr ? std::string(value) : std::string {};
        }

        // Writable directory for save slots: VULTRA_SAVE_DIR override, else the OS app-data
        // location, else the temp dir. The "Vultra/saves" subpath keeps slots grouped.
        std::filesystem::path saveDirectory()
        {
            std::filesystem::path base;
            if (const auto overrideDir = envValue("VULTRA_SAVE_DIR"); !overrideDir.empty())
                base = overrideDir;
#if defined(_WIN32)
            else if (const auto appData = envValue("APPDATA"); !appData.empty())
                base = std::filesystem::path(appData) / "Vultra" / "saves";
#else
            else if (const auto xdg = envValue("XDG_DATA_HOME"); !xdg.empty())
                base = std::filesystem::path(xdg) / "Vultra" / "saves";
            else if (const auto home = envValue("HOME"); !home.empty())
                base = std::filesystem::path(home) / ".local" / "share" / "Vultra" / "saves";
#endif
            if (base.empty())
            {
                std::error_code ec;
                base = std::filesystem::temp_directory_path(ec) / "Vultra" / "saves";
            }
            return base.lexically_normal();
        }

        // Restrict a slot name to a safe filename (no path traversal).
        std::string sanitizeSlot(const std::string& slot)
        {
            std::string out;
            out.reserve(slot.size());
            for (const char ch : slot)
            {
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')
                    out.push_back(ch);
                else
                    out.push_back('_');
            }
            return out.empty() ? std::string {"default"} : out;
        }

        std::filesystem::path slotPath(const std::string& slot)
        {
            return saveDirectory() / (sanitizeSlot(slot) + ".vsave");
        }
    } // namespace

    bool SaveSystem::onInit()
    {
        ctx().services.provide<ISaveService>(this);
        VULTRA_CORE_INFO("[SaveSystem] Initialized (save dir: {})", saveDirectory().generic_string());
        return true;
    }

    void SaveSystem::onShutdown() { m_Store.clear(); }

    void SaveSystem::setNumber(const std::string& key, double value)
    {
        m_Store[key] = Value {.type = ValueType::eNumber, .number = value};
    }
    void SaveSystem::setBool(const std::string& key, bool value)
    {
        m_Store[key] = Value {.type = ValueType::eBool, .boolean = value};
    }
    void SaveSystem::setString(const std::string& key, const std::string& value)
    {
        m_Store[key] = Value {.type = ValueType::eString, .string = value};
    }

    bool SaveSystem::has(const std::string& key) const { return m_Store.find(key) != m_Store.end(); }

    ISaveService::ValueType SaveSystem::valueType(const std::string& key) const
    {
        auto it = m_Store.find(key);
        return it != m_Store.end() ? it->second.type : ValueType::eNone;
    }

    double SaveSystem::getNumber(const std::string& key, double fallback) const
    {
        auto it = m_Store.find(key);
        return (it != m_Store.end() && it->second.type == ValueType::eNumber) ? it->second.number : fallback;
    }
    bool SaveSystem::getBool(const std::string& key, bool fallback) const
    {
        auto it = m_Store.find(key);
        return (it != m_Store.end() && it->second.type == ValueType::eBool) ? it->second.boolean : fallback;
    }
    std::string SaveSystem::getString(const std::string& key, const std::string& fallback) const
    {
        auto it = m_Store.find(key);
        return (it != m_Store.end() && it->second.type == ValueType::eString) ? it->second.string : fallback;
    }

    void SaveSystem::remove(const std::string& key) { m_Store.erase(key); }
    void SaveSystem::clear() { m_Store.clear(); }

    bool SaveSystem::save(const std::string& slot)
    {
        nlohmann::json kv = nlohmann::json::object();
        for (const auto& [key, value] : m_Store)
        {
            switch (value.type)
            {
                case ValueType::eNumber:
                    kv[key] = value.number;
                    break;
                case ValueType::eBool:
                    kv[key] = value.boolean;
                    break;
                case ValueType::eString:
                    kv[key] = value.string;
                    break;
                case ValueType::eNone:
                    break;
            }
        }
        nlohmann::json root {{"version", 1}, {"kv", std::move(kv)}};

        const auto      path = slotPath(slot);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::trunc);
        if (!file)
        {
            VULTRA_CORE_WARN("[SaveSystem] Failed to write slot '{}' to {}", slot, path.generic_string());
            return false;
        }
        file << root.dump(2);
        return true;
    }

    bool SaveSystem::load(const std::string& slot)
    {
        const auto    path = slotPath(slot);
        std::ifstream file(path);
        if (!file)
            return false;

        nlohmann::json root;
        try
        {
            file >> root;
        }
        catch (const std::exception& e)
        {
            VULTRA_CORE_WARN("[SaveSystem] Failed to parse slot '{}': {}", slot, e.what());
            return false;
        }

        m_Store.clear();
        const auto kvIt = root.find("kv");
        if (kvIt == root.end() || !kvIt->is_object())
            return true; // empty but valid

        for (const auto& [key, jval] : kvIt->items())
        {
            if (jval.is_boolean())
                setBool(key, jval.get<bool>());
            else if (jval.is_number())
                setNumber(key, jval.get<double>());
            else if (jval.is_string())
                setString(key, jval.get<std::string>());
        }
        return true;
    }

    bool SaveSystem::hasSlot(const std::string& slot) const
    {
        std::error_code ec;
        return std::filesystem::is_regular_file(slotPath(slot), ec);
    }

    bool SaveSystem::deleteSlot(const std::string& slot)
    {
        std::error_code ec;
        return std::filesystem::remove(slotPath(slot), ec);
    }

    std::vector<std::string> SaveSystem::listSlots() const
    {
        std::vector<std::string> slots;
        std::error_code          ec;
        const auto               dir = saveDirectory();
        if (!std::filesystem::is_directory(dir, ec))
            return slots;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".vsave")
                slots.push_back(entry.path().stem().generic_string());
        }
        std::sort(slots.begin(), slots.end());
        return slots;
    }
} // namespace vultra
