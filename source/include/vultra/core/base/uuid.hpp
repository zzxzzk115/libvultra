#pragma once

#include "vultra/core/base/base.hpp"

#include <vbase/core/uuid.hpp>

namespace vbase
{
    template<class Archive>
    void save(Archive& archive, const UUID& id)
    {
        std::string idStr = to_string(id);
        archive(idStr);
    }

    template<class Archive>
    void load(Archive& archive, UUID& id)
    {
        std::string idStr;
        archive(idStr);
        try_parse_uuid(idStr.c_str(), id);
    }
} // namespace vbase

namespace vultra
{
    using CoreUUID = vbase::UUID;

    class CoreUUIDHelper
    {
    public:
        static CoreUUID createStandardUUID();
        static CoreUUID getFromName(const std::string& name);
    };
} // namespace vultra
