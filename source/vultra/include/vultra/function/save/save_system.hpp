#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/function/services/save_service.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace vultra
{
    // In-memory typed KV store + JSON file slots (see ISaveService). Slots live under a
    // writable user-data directory (VULTRA_SAVE_DIR override, else the OS app-data dir).
    class SaveSystem final : public EngineSubsystem, public ISaveService
    {
    public:
        ENGINE_SUBSYSTEM(SaveSystem)

        bool onInit() override;
        void onShutdown() override;

        void setNumber(const std::string& key, double value) override;
        void setBool(const std::string& key, bool value) override;
        void setString(const std::string& key, const std::string& value) override;

        bool        has(const std::string& key) const override;
        ValueType   valueType(const std::string& key) const override;
        double      getNumber(const std::string& key, double fallback) const override;
        bool        getBool(const std::string& key, bool fallback) const override;
        std::string getString(const std::string& key, const std::string& fallback) const override;

        void remove(const std::string& key) override;
        void clear() override;

        bool                     save(const std::string& slot) override;
        bool                     load(const std::string& slot) override;
        bool                     hasSlot(const std::string& slot) const override;
        bool                     deleteSlot(const std::string& slot) override;
        std::vector<std::string> listSlots() const override;

    private:
        struct Value
        {
            ValueType   type {ValueType::eNone};
            double      number {0.0};
            bool        boolean {false};
            std::string string;
        };

        std::unordered_map<std::string, Value> m_Store;
    };
} // namespace vultra
