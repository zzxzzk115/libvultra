#pragma once
#include <vbase/core/uuid.hpp>

#include <cereal/cereal.hpp>

#include <string>

namespace vultra
{
    // Engine-facing UUID type.
    // Wraps vbase::UUID so the engine doesn't leak vbase types everywhere.
    struct CoreUUID
    {
        vbase::UUID value {};

        CoreUUID() = default;
        explicit CoreUUID(const vbase::UUID& v) : value(v) {}

        [[nodiscard]] bool        valid() const { return value != vbase::UUID {}; }
        [[nodiscard]] std::string toString() const { return vbase::to_string(value); }

        [[nodiscard]] vbase::UUID&       native() { return value; }
        [[nodiscard]] const vbase::UUID& native() const { return value; }

        // Compatibility bridge for existing code that still accepts vbase::UUID.
        // Prefer native() for new code.
        operator vbase::UUID&() { return value; }
        operator const vbase::UUID&() const { return value; }

        friend bool operator==(const CoreUUID& a, const CoreUUID& b) { return a.value == b.value; }
        friend bool operator!=(const CoreUUID& a, const CoreUUID& b) { return a.value != b.value; }
    };

    // Cereal: keep UUIDs human-readable.
    template<class Archive>
    void save(Archive& archive, const CoreUUID& id)
    {
        std::string idStr = id.toString();
        archive(idStr);
    }

    template<class Archive>
    void load(Archive& archive, CoreUUID& id)
    {
        std::string idStr;
        archive(idStr);
        vbase::UUID tmp {};
        vbase::try_parse_uuid(idStr.c_str(), tmp);
        id.value = tmp;
    }

    class CoreUUIDHelper
    {
    public:
        static CoreUUID createStandardUUID();
        static CoreUUID getFromName(const std::string& name);
    };
} // namespace vultra

namespace std
{
    template<>
    struct hash<vultra::CoreUUID>
    {
        size_t operator()(const vultra::CoreUUID& id) const noexcept { return std::hash<vbase::UUID> {}(id.value); }
    };
} // namespace std
