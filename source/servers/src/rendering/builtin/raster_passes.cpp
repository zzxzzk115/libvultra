#include <vultra/servers/rendering/builtin/raster_passes.hpp>
#include <vultra/servers/rendering/builtin/tone_mapping_pass.hpp>

#include <algorithm>
#include <array>

namespace vultra
{
    std::expected<void, std::string> installBuiltinRenderPassesExpected(PassCatalog& catalog)
    {
        auto definitions = builtinRasterPassDefinitions();
        definitions.insert(definitions.begin(), toneMappingDefinition());
        return catalog.addDefinitionsExpected(std::move(definitions));
    }

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

            std::expected<std::vector<RenderGraph::Resource>, std::string>
            addPassesExpected(RenderGraph& graph,
                              std::string_view,
                              std::span<const RenderGraph::Resource> inputs,
                              std::span<const double>                parameters) override
            {
                if (!m_Renderer || !m_Bindings)
                {
                    return std::unexpected("Built-in raster pass requires bindBuiltinRasterPassesExpected()");
                }
                const size_t inputCount = m_Stage == RasterStage::eDeferred ? 13 : 0;
                if (inputs.size() != inputCount || !parameters.empty())
                {
                    return std::unexpected("Built-in raster pass input/parameter count mismatch");
                }
                auto                               outputs = *m_Bindings;
                std::vector<RenderGraph::Resource> result;
                switch (m_Stage)
                {
                    case RasterStage::eShadow: {
                        if (outputs.shadows[0].graph)
                        {
                            return std::unexpected("One shadow stage per renderer view");
                        }
                        auto shadows = m_Renderer->addShadowPassesExpected(graph);
                        if (!shadows)
                        {
                            return std::unexpected(shadows.error());
                        }
                        outputs.shadows = *shadows;
                        result.assign(outputs.shadows.begin(), outputs.shadows.end());
                        break;
                    }
                    case RasterStage::eSkybox: {
                        if (outputs.hdr.graph)
                        {
                            return std::unexpected("One skybox stage per renderer view");
                        }
                        auto hdr =
                            graph.createTextureExpected("scene_hdr", colorTexture(m_Size, VriFormat_RGBA16_SFLOAT));
                        if (!hdr)
                        {
                            return std::unexpected(hdr.error());
                        }
                        outputs.hdr = *hdr;
                        if (auto sky = m_Renderer->addSkyboxPassExpected(graph, outputs.hdr); !sky)
                        {
                            return std::unexpected(sky.error());
                        }
                        result = {outputs.hdr};
                        break;
                    }
                    case RasterStage::eGBuffer: {
                        if (outputs.path != RenderPath::eNaiveDeferred || m_Renderer->settings.meshShading)
                        {
                            return std::unexpected("Built-in G-buffer requires indexed deferred geometry");
                        }
                        if (outputs.depth.graph)
                        {
                            return std::unexpected("One geometry stage per renderer view");
                        }
                        auto desc = depthTexture(m_Size);
                        desc.usage |= VriTextureUsage_TransferSrc;
                        auto depth = graph.createTextureExpected("scene_depth", desc);
                        if (!depth)
                        {
                            return std::unexpected(depth.error());
                        }
                        outputs.depth = *depth;
                        auto gbuffer  = m_Renderer->addGBufferPassesExpected(graph, outputs.depth, m_Size);
                        if (!gbuffer)
                        {
                            return std::unexpected(gbuffer.error());
                        }
                        outputs.gbuffer = *gbuffer;
                        result.assign(outputs.gbuffer.begin(), outputs.gbuffer.end());
                        result.push_back(outputs.depth);
                        break;
                    }
                    case RasterStage::eDeferred: {
                        if (outputs.path != RenderPath::eNaiveDeferred)
                        {
                            return std::unexpected("Deferred lighting requires a deferred renderer view");
                        }
                        outputs.hdr   = inputs[0];
                        outputs.depth = inputs[1];
                        std::copy_n(inputs.begin() + 2, 7, outputs.gbuffer.begin());
                        std::copy_n(inputs.begin() + 9, 4, outputs.shadows.begin());
                        if (auto added = m_Renderer->addDeferredLightingPassExpected(graph,
                                                                                     outputs.hdr,
                                                                                     outputs.gbuffer,
                                                                                     outputs.shadows);
                            !added)
                        {
                            return std::unexpected(added.error());
                        }
                        result = {outputs.hdr};
                        break;
                    }
                    default:
                        return std::unexpected("Unknown raster stage");
                }
                *m_Bindings = outputs;
                return result;
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
            definition.createExpected =
                [stage = RasterStage(i)](Device&) -> std::expected<std::unique_ptr<GraphPass>, std::string>
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

    std::expected<void, std::string> bindBuiltinRasterPassesExpected(PassCatalog&              catalog,
                                                                     BuiltinRenderer&          renderer,
                                                                     BuiltinRenderer::Outputs& bindings,
                                                                     Extent                    size)
    {
        for (const auto type : types)
        {
            if (auto found = catalog.definitionExpected(type); !found)
            {
                return std::unexpected(found.error());
            }
        }
        for (uint32_t i = 0; i < types.size(); ++i)
        {
            auto bound = catalog.bindFactoryExpected(
                types[i],
                [&, stage = RasterStage(i), size](Device&) -> std::expected<std::unique_ptr<GraphPass>, std::string>
                {
                    return std::make_unique<RasterPass>(stage, &renderer, &bindings, size);
                });
            if (!bound)
            {
                return bound;
            }
        }
        return {};
    }
} // namespace vultra
