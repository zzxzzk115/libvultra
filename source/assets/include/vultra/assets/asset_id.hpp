#pragma once

#include <vultra/core/base/stable_id.hpp>

namespace vultra
{
    struct AssetId
    {
        StableId    value;
        friend bool operator==(const AssetId&, const AssetId&) = default;
    };
} // namespace vultra
