#pragma once

#include <vultra/assets/shader_property.hpp>

#include <cstddef>
#include <expected>
#include <map>
#include <string_view>

namespace vultra
{
    struct ShaderAsset;
    struct ShaderProgram;
    class ShaderUniformBlock;

    // Persistence uses material_values; asset-aware setters and uniformData additionally require game_shader_runtime.
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
        std::expected<ShaderPropertyValue, std::string> valueExpected(const ShaderAsset& asset,
                                                                      std::string_view   name) const;
        std::expected<void, std::string>
        setExpected(const ShaderAsset& asset, std::string_view name, ShaderPropertyValue value);
        std::expected<void, std::string> resetExpected(std::string_view name);
        std::expected<void, std::string> setVariantExpected(const ShaderAsset& asset, std::string value);
        std::expected<void, std::string> validateExpected(const ShaderAsset& asset) const;
        std::expected<void, std::string> assignExpected(const ShaderAsset& asset, const MaterialInstance& source);
        // Rejection preserves overrides, selected variant, revision and caller diagnostics.
        std::expected<void, std::string>
        reconcileExpected(const ShaderAsset& previous, const ShaderAsset& candidate, std::string& diagnostics);
        std::expected<std::vector<std::byte>, std::string> uniformDataExpected(const ShaderAsset&   asset,
                                                                               const ShaderProgram& program) const;
        std::expected<ShaderUniformBlock, std::string>     uniformBlockExpected(const ShaderAsset&   asset,
                                                                                const ShaderProgram& program) const;
        uint64_t revision() const;
        std::vector<std::byte>  uniformData(const ShaderAsset& asset, const ShaderProgram& program) const;
        std::expected<std::string, std::string> serializeExpected() const;
        static std::expected<MaterialInstance, std::string> parseExpected(std::string_view text);
        std::string             serialize() const;
        static MaterialInstance parse(std::string_view text);

    private:
        std::map<std::string, ShaderPropertyValue, std::less<>> m_Overrides;
        uint64_t                                                m_Revision = 0;
    };
} // namespace vultra
