#include <vultra/servers/rendering/builtin/raster_passes.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        enum class RasterStage
        {
            eShadow,
            eSkybox,
            eGBuffer,
            eDeferred
        };
        constexpr std::array types {"vultra.shadow", "vultra.skybox", "vultra.gbuffer", "vultra.deferred_lighting"};
        constexpr std::array names {"position_metallic",
                                    "normal_roughness",
                                    "albedo_weight",
                                    "emission_occlusion",
                                    "specular",
                                    "geometric_normal_ior",
                                    "coat"};

        class RasterPass final : public GraphPass
        {
        public:
            RasterPass(RasterStage               stage,
                       BuiltinRenderer*          renderer = nullptr,
                       BuiltinRenderer::Outputs* bindings = nullptr,
                       Extent                    size     = {}) :
                m_Stage(stage),
                m_Renderer(renderer),
                m_Bindings(bindings),
                m_Size(size)
            {
            }

            std::vector<RenderGraph::Resource> addPasses(RenderGraph& graph,
                                                         std::string_view,
                                                         std::span<const RenderGraph::Resource> inputs,
                                                         std::span<const double>) override
            {
                if (!m_Renderer || !m_Bindings)
                {
                    throw std::invalid_argument("Built-in raster pass requires bindBuiltinRasterPasses()");
                }
                auto& outputs = *m_Bindings;
                switch (m_Stage)
                {
                    case RasterStage::eShadow:
                        if (outputs.shadows[0].graph)
                        {
                            throw std::invalid_argument("One shadow stage per renderer view");
                        }
                        outputs.shadows = m_Renderer->addShadowPasses(graph);
                        return {outputs.shadows.begin(), outputs.shadows.end()};
                    case RasterStage::eSkybox:
                        if (outputs.hdr.graph)
                        {
                            throw std::invalid_argument("One skybox stage per renderer view");
                        }
                        outputs.hdr = graph.createTexture("scene_hdr", colorTexture(m_Size, VriFormat_RGBA16_SFLOAT));
                        m_Renderer->addSkyboxPass(graph, outputs.hdr);
                        return {outputs.hdr};
                    case RasterStage::eGBuffer: {
                        if (outputs.path != RenderPath::eNaiveDeferred || m_Renderer->settings.meshShading)
                        {
                            throw std::invalid_argument("Built-in G-buffer requires indexed deferred geometry");
                        }
                        if (outputs.depth.graph)
                        {
                            throw std::invalid_argument("One geometry stage per renderer view");
                        }
                        auto desc = depthTexture(m_Size);
                        desc.usage |= VriTextureUsage_TransferSrc;
                        outputs.depth   = graph.createTexture("scene_depth", desc);
                        outputs.gbuffer = m_Renderer->addGBufferPasses(graph, outputs.depth, m_Size);
                        std::vector<RenderGraph::Resource> result(outputs.gbuffer.begin(), outputs.gbuffer.end());
                        result.push_back(outputs.depth);
                        return result;
                    }
                    case RasterStage::eDeferred: {
                        if (outputs.path != RenderPath::eNaiveDeferred)
                        {
                            throw std::invalid_argument("Deferred lighting requires a deferred renderer view");
                        }
                        outputs.hdr   = inputs[0];
                        outputs.depth = inputs[1];
                        std::copy_n(inputs.begin() + 2, 7, outputs.gbuffer.begin());
                        std::copy_n(inputs.begin() + 9, 4, outputs.shadows.begin());
                        m_Renderer->addDeferredLightingPass(graph, outputs.hdr, outputs.gbuffer, outputs.shadows);
                        return {outputs.hdr};
                    }
                }
                throw std::logic_error("Unknown raster stage");
            }

        private:
            RasterStage               m_Stage;
            BuiltinRenderer*          m_Renderer;
            BuiltinRenderer::Outputs* m_Bindings;
            Extent                    m_Size;
        };
    } // namespace

    std::vector<PassDefinition> builtinRasterPassDefinitions()
    {
        std::vector<PassDefinition> result;
        for (uint32_t i = 0; i < types.size(); ++i)
        {
            PassDefinition definition;
            definition.type   = types[i];
            definition.create = [stage = RasterStage(i)](Device&)
            {
                return std::make_unique<RasterPass>(stage);
            };
            if (i == 0)
            {
                for (uint32_t cascade = 0; cascade < 4; ++cascade)
                {
                    definition.outputs.push_back({"cascade" + std::to_string(cascade),
                                                  PassResourceKind::eTexture,
                                                  VriFormat_D32_SFLOAT,
                                                  std::nullopt});
                }
            }
            else if (i == 1)
            {
                definition.outputs = {{"hdr", PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT, std::nullopt}};
            }
            else if (i == 2)
            {
                for (uint32_t port = 0; port < names.size(); ++port)
                {
                    definition.outputs.push_back({names[port],
                                                  PassResourceKind::eTexture,
                                                  port == 0 ? VriFormat_RGBA32_SFLOAT : VriFormat_RGBA16_SFLOAT,
                                                  std::nullopt});
                }
                definition.outputs.push_back({"depth", PassResourceKind::eTexture, VriFormat_D32_SFLOAT, std::nullopt});
            }
            else
            {
                definition.inputs = {{"hdr", PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT, std::nullopt},
                                     {"depth", PassResourceKind::eTexture, VriFormat_D32_SFLOAT, 0}};
                for (uint32_t port = 0; port < names.size(); ++port)
                {
                    definition.inputs.push_back({names[port],
                                                 PassResourceKind::eTexture,
                                                 port == 0 ? VriFormat_RGBA32_SFLOAT : VriFormat_RGBA16_SFLOAT,
                                                 0});
                }
                for (uint32_t cascade = 0; cascade < 4; ++cascade)
                {
                    definition.inputs.push_back(
                        {"cascade" + std::to_string(cascade), PassResourceKind::eTexture, VriFormat_D32_SFLOAT, 9});
                }
                definition.outputs = {{"hdr", PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT, 0}};
            }
            result.push_back(std::move(definition));
        }
        return result;
    }

    bool usesBuiltinRasterPasses(const GraphDefinition& definition)
    {
        return std::ranges::any_of(definition.passes,
                                   [](const auto& pass)
                                   {
                                       return std::ranges::find(types, pass.type) != types.end();
                                   });
    }

    void bindBuiltinRasterPasses(PassCatalog&              catalog,
                                 BuiltinRenderer&          renderer,
                                 BuiltinRenderer::Outputs& bindings,
                                 Extent                    size)
    {
        for (uint32_t i = 0; i < types.size(); ++i)
        {
            catalog.bindFactory(types[i],
                                [&, stage = RasterStage(i), size](Device&)
                                {
                                    return std::make_unique<RasterPass>(stage, &renderer, &bindings, size);
                                });
        }
    }
} // namespace vultra
