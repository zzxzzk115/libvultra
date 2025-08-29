#pragma once

#include "vultra/core/base/base.hpp"

#include <uuid.h>

namespace uuids
{
    template<class Archive>
    void save(Archive& archive, const uuid& id)
    {
        std::string idStr = to_string(id);
        archive(idStr);
    }

    template<class Archive>
    void load(Archive& archive, uuid& id)
    {
        std::string idStr;
        archive(idStr);
        id = uuid::from_string(idStr).value();
    }
} // namespace uuids

namespace vultra
{
    using CoreUUID = uuids::uuid;

    class CoreUUIDHelper
    {
    public:
        static CoreUUID createStandardUUID();
        static CoreUUID getFromName(const std::string& name);

    private:
        static Ref<uuids::uuid_name_generator> s_NameUUIDGenerator;
    };
} // namespace vultra
