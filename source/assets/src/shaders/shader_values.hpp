#pragma once

#include <vultra/assets/shader_asset.hpp>

#include <nlohmann/json.hpp>

namespace vultra::detail
{
    nlohmann::json      encodePropertyValue(const ShaderPropertyValue& value);
    ShaderPropertyValue decodePropertyValue(const nlohmann::json& document);
} // namespace vultra::detail
