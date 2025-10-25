#pragma once

#include "vultra/core/base/base.hpp"

#include <vasset/vuuid.hpp>

namespace vasset
{
    template<class Archive>
    void save(Archive& archive, const VUUID& id)
    {
        std::string idStr = id.toString();
        archive(idStr);
    }

    template<class Archive>
    void load(Archive& archive, VUUID& id)
    {
        std::string idStr;
        archive(idStr);
        id = VUUID::fromString(idStr);
    }
} // namespace vasset

namespace vultra
{
    using CoreUUID = vasset::VUUID;

    class CoreUUIDHelper
    {
    public:
        static CoreUUID createStandardUUID();
        static CoreUUID getFromName(const std::string& name);
    };
} // namespace vultra
