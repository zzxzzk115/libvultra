#pragma once

#include <vultra/assets/shader_asset.hpp>
#include <vultra/drivers/rhi/resources.hpp>

#include <functional>
#include <memory>

namespace vultra
{
    struct ShaderPassContext
    {
        ShaderPassContext() = default;
        ShaderPassContext(const ShaderPassContext& other);
        ShaderPassContext& operator=(const ShaderPassContext& other);
        ShaderPassContext(ShaderPassContext&&) noexcept                             = default;
        ShaderPassContext&                  operator=(ShaderPassContext&&) noexcept = default;
        std::vector<VriFormat>              colors;
        VriFormat                           depth = VriFormat_Unknown;
        std::vector<VriVertexAttributeDesc> attributes;
        std::vector<VriVertexStreamDesc>    streams;
        VriPrimitiveTopology                topology      = VriPrimitiveTopology_TriangleList;
        uint32_t                            samples       = 1;
        uint32_t                            viewMask      = 0;
        bool                                mirrored      = false;
        bool                                depthReadOnly = false;

    private:
        // Cached contexts own semantic strings as well as the VRI descriptor arrays.
        std::vector<std::string> m_SemanticNames;
    };

    struct ShaderResourceViews
    {
        std::string                       name;
        VriDescriptorType                 type = VriDescriptorType_Texture;
        std::vector<const VriDescriptor*> views;
    };

    struct ShaderTextureBinding
    {
        VriDescriptor*     texture   = nullptr;
        VriDescriptor*     sampler   = nullptr;
        VriTextureViewType dimension = VriTextureViewType_2D;
        bool               srgb      = false;
        bool               bc5       = false;
    };

    // Owns instance GPU parameters and cached pipelines. CPU asset/instance and external views must outlive it.
    class ShaderMaterial
    {
    public:
        using TextureResolver = std::function<ShaderTextureBinding(const ShaderProperty&, const ShaderTextureValue&)>;
        ShaderMaterial(Device&                                    device,
                       const ShaderAsset&                         asset,
                       MaterialInstance&                          instance,
                       TextureResolver                            textures           = {},
                       std::span<const std::string_view>          requiredLightModes = {},
                       const ShaderAsset::SubshaderCompatibility& compatible         = {});
        ~ShaderMaterial();
        ShaderMaterial(const ShaderMaterial&)            = delete;
        ShaderMaterial& operator=(const ShaderMaterial&) = delete;

        // Previous GPU frame must be complete. Prepare outside recording; bind never creates GPU objects.
        VriPipeline*      prepare(std::string_view                     pass,
                                  const ShaderPassContext&             context,
                                  std::span<const ShaderResourceViews> resources = {});
        void              bind(VriCommandBuffer* commands, std::string_view pass) const;
        void              bind(VriCommandBuffer* commands, VriPipeline* prepared) const;
        const ShaderPass& passForMode(std::string_view mode, bool mesh = false) const;
        uint32_t          pushConstantSize(std::string_view pass) const;
        size_t            pipelineCount() const;
        uint32_t          subshader() const;

        struct PreparedPass
        {
            std::string                      name;
            ShaderPassContext                context;
            std::vector<ShaderResourceViews> resources;
        };

        // Only the latest prepared state per Pass. External views remain borrowed from the current host frame.
        std::vector<PreparedPass> preparedPasses() const;

    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
} // namespace vultra
