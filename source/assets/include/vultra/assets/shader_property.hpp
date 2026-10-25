#pragma once

#include <vultra/assets/asset_id.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace vultra
{
    enum class ShaderPropertyType
    {
        eFloat,
        eRange,
        eInteger,
        eBoolean,
        eVector,
        eColor,
        eTexture2D,
        eTexture2DArray,
        eTexture3D,
        eTextureCube,
        eTextureCubeArray
    };

    struct ShaderTextureValue
    {
        AssetId              asset {};
        std::string          builtin;
        std::array<float, 4> scaleOffset {1, 1, 0, 0};
        friend bool          operator==(const ShaderTextureValue&, const ShaderTextureValue&) = default;
    };

    using ShaderPropertyValue = std::variant<float, int32_t, bool, std::array<float, 4>, ShaderTextureValue>;

    struct ShaderSourceLocation
    {
        std::filesystem::path file;
        uint32_t              line   = 1;
        uint32_t              column = 1;
        std::string           describe() const;
    };

    struct ShaderProperty
    {
        std::string                                  name;
        std::string                                  label;
        ShaderPropertyType                           type          = ShaderPropertyType::eFloat;
        ShaderPropertyValue                          defaultValue  = 0.0f;
        bool                                         hdr           = false;
        bool                                         normal        = false;
        bool                                         srgb          = false;
        bool                                         hidden        = false;
        bool                                         noScaleOffset = false;
        double                                       minimum       = 0;
        double                                       maximum       = 1;
        std::vector<std::pair<std::string, int32_t>> enumeration;
        ShaderSourceLocation                         location;
        bool                                         isTexture() const;
        void                                         validate(const ShaderPropertyValue& value) const;
    };

} // namespace vultra
