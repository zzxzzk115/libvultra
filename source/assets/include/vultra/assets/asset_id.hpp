#pragma once

#include <vultra/core/base/api_annotations.hpp>
#include <vultra/core/base/stable_id.hpp>

namespace vultra
{
    struct VULTRA_REFERENCE("asset") AssetId
    {
        StableId    value;
        friend bool operator==(const AssetId&, const AssetId&) = default;
    };
} // namespace vultra
