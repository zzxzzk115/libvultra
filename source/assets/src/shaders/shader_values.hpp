#pragma once

#include <vultra/assets/material_instance.hpp>

#include <nlohmann/json.hpp>

namespace vultra::detail
{
    nlohmann::json      encodePropertyValue(const ShaderPropertyValue& value);
    std::expected<ShaderPropertyValue, std::string> decodePropertyValueExpected(const nlohmann::json& document);
    ShaderPropertyValue decodePropertyValue(const nlohmann::json& document);
} // namespace vultra::detail
