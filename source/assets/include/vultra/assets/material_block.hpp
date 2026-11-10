#pragma once

#include <vultra/assets/asset_id.hpp>
#include <vultra/drivers/rhi/shader_bindings.hpp>

#include <functional>
#include <map>

namespace vultra
{
    struct MaterialUniformWrite
    {
        std::string_view                    binding;
        std::span<const ShaderUniformWrite> fields;
    };

    struct MaterialResourceWrite
    {
        std::string_view         binding;
        std::span<const AssetId> assets;
    };

    struct MaterialResourceValue
    {
        VriDescriptorType    type;
        VriTextureViewType   dimension;
        std::vector<AssetId> assets; // Zero IDs are explicitly unassigned, not null native descriptors.
        bool                 operator==(const MaterialResourceValue&) const = default;
    };

    struct MaterialResourceView
    {
        const VriDescriptor* view      = nullptr;
        VriDescriptorType    type      = VriDescriptorType_Texture;
        VriTextureViewType   dimension = VriTextureViewType_2D;
    };

    // Reflected CPU values and symbolic resources. GPU storage remains with ShaderBindings and resource owners.
    class MaterialBlock
    {
    public:
        using Resolver = std::function<std::expected<MaterialResourceView, std::string>(std::string_view, AssetId)>;
        static std::expected<MaterialBlock, std::string> create(const ShaderProgram& program, std::string_view scope);
        // Stages all named edits before publishing any values or advancing the block revision once.
        std::expected<bool, std::string> setExpected(std::span<const MaterialUniformWrite>  uniforms,
                                                     std::span<const MaterialResourceWrite> resources = {});
        // Independent candidate: migrate compatible numeric fields and same-kind/dimension resource prefixes.
        std::expected<MaterialBlock, std::string> reconcileExpected(const ShaderProgram& program,
                                                                    std::string&         diagnostics) const;
        // Full resource batch only. Views stay borrowed; the caller retains owners and completes prior GPU uses.
        std::expected<std::vector<ShaderResourceViews>, std::string>
                                                         resolveResourcesExpected(const Resolver& resolver) const;
        std::expected<std::string, std::string>          serializeExpected() const;
        static std::expected<MaterialBlock, std::string> parseExpected(std::string_view text);
        const std::string&                               scope() const;
        // ConstantBuffer arrays expose one independent block per flattened descriptor element as binding[i].
        const std::map<std::string, ShaderUniformBlock, std::less<>>&    uniforms() const;
        const std::map<std::string, MaterialResourceValue, std::less<>>& resources() const;
        uint64_t                                                         revision() const;

    private:
        explicit MaterialBlock(std::string scope);
        std::string                                               m_Scope;
        std::map<std::string, ShaderUniformBlock, std::less<>>    m_Uniforms;
        std::map<std::string, MaterialResourceValue, std::less<>> m_Resources;
        uint64_t                                                  m_Revision = 0;
    };
} // namespace vultra
