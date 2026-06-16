#pragma once

#include <vbase/service/service_registry.hpp>

#include <string>
#include <vector>

namespace vultra
{
    // A simple persistence layer: a typed key/value store plus named save slots backed by JSON
    // files under a writable user-data directory. Exposed to Lua as the `Save` namespace.
    //
    // The KV store lives in memory and survives scene loads/reloads (the service is an engine
    // subsystem, not world data); `save(slot)`/`load(slot)` persist/restore it to/from disk.
    class ISaveService
    {
    public:
        SERVICE_REGISTER(ISaveService)

        virtual ~ISaveService() = default;

        // Value type stored at a key: 0 none, 1 number, 2 bool, 3 string.
        enum class ValueType : int
        {
            eNone   = 0,
            eNumber = 1,
            eBool   = 2,
            eString = 3,
        };

        // --- Key/value store ---
        virtual void setNumber(const std::string& key, double value) = 0;
        virtual void setBool(const std::string& key, bool value)     = 0;
        virtual void setString(const std::string& key, const std::string& value) = 0;

        virtual bool        has(const std::string& key) const = 0;
        virtual ValueType   valueType(const std::string& key) const = 0;
        virtual double      getNumber(const std::string& key, double fallback) const = 0;
        virtual bool        getBool(const std::string& key, bool fallback) const = 0;
        virtual std::string getString(const std::string& key, const std::string& fallback) const = 0;

        virtual void remove(const std::string& key) = 0;
        virtual void clear() = 0;

        // --- Named slots (persisted to disk) ---
        virtual bool                     save(const std::string& slot) = 0;
        virtual bool                     load(const std::string& slot) = 0;
        virtual bool                     hasSlot(const std::string& slot) const = 0;
        virtual bool                     deleteSlot(const std::string& slot) = 0;
        virtual std::vector<std::string> listSlots() const = 0;
    };
} // namespace vultra
