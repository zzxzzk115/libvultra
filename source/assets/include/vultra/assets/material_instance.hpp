#pragma once

#include <vultra/assets/shader_property.hpp>

#include <cstddef>
#include <map>
#include <string_view>

namespace vultra
{
    struct ShaderAsset;
    struct ShaderProgram;

    // Persistence uses material_values; asset-aware setters and uniformData additionally require game_shader.
    class MaterialInstance
    {
    public:
        AssetId                                                        shader {};
        std::string                                                    variant = "Default";
        const std::map<std::string, ShaderPropertyValue, std::less<>>& overrides() const;
        ShaderPropertyValue value(const ShaderAsset& asset, std::string_view name) const;
        void                set(const ShaderAsset& asset, std::string_view name, ShaderPropertyValue value);
        void                reset(std::string_view name);
        void                setVariant(const ShaderAsset& asset, std::string value);
        void                validate(const ShaderAsset& asset) const;
        void                assign(const ShaderAsset& asset, const MaterialInstance& source);
        void     reconcile(const ShaderAsset& previous, const ShaderAsset& candidate, std::string& diagnostics);
        uint64_t revision() const;
        std::vector<std::byte>  uniformData(const ShaderAsset& asset, const ShaderProgram& program) const;
        std::string             serialize() const;
        static MaterialInstance parse(std::string_view text);

    private:
        std::map<std::string, ShaderPropertyValue, std::less<>> m_Overrides;
        uint64_t                                                m_Revision = 0;
    };
} // namespace vultra
