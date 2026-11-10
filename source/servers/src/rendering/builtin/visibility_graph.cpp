#include <vultra/servers/rendering/builtin/clustered_lights.hpp>
#include <vultra/servers/rendering/builtin/frame_data.hpp>
#include <vultra/servers/rendering/builtin/visibility_graph.hpp>

#include <algorithm>
#include <set>

namespace vultra
{
    std::expected<std::unique_ptr<VisibilityColorInput>, std::string> VisibilityColorInput::create(Device& device)
    {
        auto       result  = std::unique_ptr<VisibilityColorInput>(new VisibilityColorInput(device));
        const auto queried = vriGetInterface(device.handle,
                                             VRI_INTERFACE_COLOR_INPUT,
                                             sizeof(result->m_Interface),
                                             &result->m_Interface);
        if (queried != VriResult_Success)
        {
            return std::unexpected("Query visibility color input failed: " + std::to_string(queried));
        }
        const VriColorInputPassDesc desc {VriFormat_RG32_UINT, VriFormat_RGBA16_SFLOAT, VriFormat_D32_SFLOAT};
        const auto                  created = result->m_Interface.CreatePass(device.handle, &desc, &result->m_Pass);
        if (created != VriResult_Success || !result->m_Pass)
        {
            return std::unexpected("Create visibility color input pass failed: " + std::to_string(created));
        }
        return result;
    }

    VisibilityColorInput::~VisibilityColorInput()
    {
        if (m_Target)
        {
            m_Interface.DestroyTarget(m_Target);
        }
        if (m_Pass)
        {
            m_Interface.DestroyPass(m_Pass);
        }
    }

    std::expected<void, std::string> VisibilityColorInput::attach(Texture& ids, Texture& depth, Texture& hdr)
    {
        if (m_Target || !ids.belongsTo(m_Device) || !depth.belongsTo(m_Device) || !hdr.belongsTo(m_Device))
        {
            return std::unexpected("Visibility color input requires three unattached views on its device");
        }
        auto idView = ids.viewExpected();
        if (!idView)
        {
            return std::unexpected(idView.error());
        }
        auto hdrView = hdr.viewExpected();
        if (!hdrView)
        {
            return std::unexpected(hdrView.error());
        }
        auto depthView = depth.viewExpected();
        if (!depthView)
        {
            return std::unexpected(depthView.error());
        }
        const auto created = m_Interface.CreateTarget(m_Pass, *idView, *hdrView, *depthView, &m_Target);
        if (created != VriResult_Success || !m_Target)
        {
            return std::unexpected("Create visibility color input target failed: " + std::to_string(created));
        }
        m_Images = {&ids, &depth, &hdr};
        return {};
    }

    namespace
    {
        std::expected<void, std::string> validateShadows(RenderGraph& graph, const VisibilityGraphInputs& inputs)
        {
            const bool present = std::ranges::any_of(inputs.shadows,
                                                     [](auto resource)
                                                     {
                                                         return resource.graph;
                                                     });
            if (!present)
                return {};
            for (size_t i = 0; i < inputs.shadows.size(); ++i)
            {
                auto texture = graph.getTextureExpected(inputs.shadows[i]);
                if (!texture || (*texture)->desc.format != VriFormat_D32_SFLOAT ||
                    !((*texture)->desc.usage & VriTextureUsage_ShaderResource))
                    return std::unexpected("Visibility shadow maps must be imported sampleable D32 textures");
                auto view = (*texture)->viewExpected();
                if (!view || *view != inputs.lighting[i + 1])
                    return std::unexpected("Visibility shadow map descriptors do not match graph resources");
                for (size_t j = 0; j < i; ++j)
                    if (inputs.shadows[j].index == inputs.shadows[i].index)
                        return std::unexpected("Visibility shadow maps must be distinct");
            }
            return {};
        }

        std::expected<void, std::string> validateSampledResources(RenderGraph&                   graph,
                                                                  const VisibilitySceneBindings& scene,
                                                                  const VisibilityGraphInputs&   inputs)
        {
            auto frame = graph.getBufferExpected(inputs.frame);
            if (!frame || !((*frame)->desc.usage & VriBufferUsage_ConstantBuffer) ||
                (*frame)->desc.size < sizeof(BuiltinFrameData))
                return std::unexpected("Visibility graph requires a complete frame constant buffer");

            std::set<const Texture*> expected;
            for (const auto& material : scene.materialTextures())
                for (const auto* texture : material)
                    expected.insert(texture);
            if (expected.contains(nullptr) || inputs.materialTextures.size() != expected.size())
                return std::unexpected("Visibility graph must import every distinct material texture");
            for (auto resource : inputs.materialTextures)
            {
                auto texture = graph.getTextureExpected(resource);
                if (!texture || !((*texture)->desc.usage & VriTextureUsage_ShaderResource) ||
                    expected.erase(*texture) != 1)
                    return std::unexpected("Visibility graph material textures do not match scene bindings");
            }
            constexpr std::array<size_t, 10> slots {1, 2, 3, 4, 5, 6, 7, 8, 9, 11};
            for (size_t i = 0; i < slots.size(); ++i)
            {
                auto texture = graph.getTextureExpected(inputs.lightingTextures[i]);
                if (!texture || !((*texture)->desc.usage & VriTextureUsage_ShaderResource))
                    return std::unexpected("Visibility graph requires sampled lighting textures");
                auto view = (*texture)->viewExpected();
                if (!view || *view != inputs.lighting[slots[i]])
                    return std::unexpected("Visibility graph lighting descriptors do not match textures");
            }
            return {};
        }

        void appendSampledResources(std::vector<RenderGraph::Use>& uses,
                                    const VisibilityGraphInputs&   inputs,
                                    bool                           includeLighting)
        {
            uses.push_back({inputs.frame, Usage::eConstantRead});
            const auto append = [&uses](RenderGraph::Resource resource)
            {
                if (std::ranges::none_of(uses,
                                         [resource](const auto& use)
                                         {
                                             return use.resource.graph == resource.graph &&
                                                    use.resource.index == resource.index &&
                                                    use.usage == Usage::eSampled;
                                         }))
                    uses.push_back({resource, Usage::eSampled});
            };
            for (auto texture : inputs.materialTextures)
                append(texture);
            if (includeLighting)
                for (auto texture : inputs.lightingTextures)
                    append(texture);
        }

        std::expected<void, std::string> validateClusters(RenderGraph&                     graph,
                                                          const VisibilityResolvePipeline& pipeline,
                                                          const VisibilitySceneBindings&   scene,
                                                          RenderGraph::Resource            resource,
                                                          RenderGraph::Resource            ids)
        {
            if (!pipeline.clustered())
                return {};
            auto buffer = graph.getBufferExpected(resource);
            if (!buffer || *buffer != scene.clusterBuffer())
                return std::unexpected("Visibility graph must import its exact clustered light buffer");
            const auto info = graph.resourceInfo(ids);
            if (!info.isTexture || !info.textureDesc.width || !info.textureDesc.height)
                return std::unexpected("Clustered visibility requires a nonempty ID texture");
            const uint64_t columns =
                (uint64_t(info.textureDesc.width) + ClusteredLights::kTileSize - 1) / ClusteredLights::kTileSize;
            const uint64_t rows =
                (uint64_t(info.textureDesc.height) + ClusteredLights::kTileSize - 1) / ClusteredLights::kTileSize;
            if (columns * rows * ClusteredLights::kDepthSlices * 8 > (*buffer)->desc.size)
                return std::unexpected("Clustered light buffer is too small for the visibility extent");
            return {};
        }

        std::expected<void, std::string> validateInputs(RenderGraph&                     graph,
                                                        const VisibilityRasterPipeline&  raster,
                                                        const VisibilityResolvePipeline& resolve,
                                                        VisibilitySceneBindings&         scene,
                                                        const VisibilityGraphInputs&     inputs)
        {
            auto& device = graph.device();
            if (!raster.belongsTo(device) || !scene.belongsTo(device, resolve) || !inputs.frameSet || !inputs.demand ||
                !inputs.repair || !graph.ownsResource(inputs.ids) || !graph.ownsResource(inputs.depth) ||
                !graph.ownsResource(inputs.hdr) ||
                std::ranges::any_of(inputs.lighting,
                                    [](const auto* descriptor)
                                    {
                                        return !descriptor;
                                    }))
            {
                return std::unexpected("Invalid visibility graph inputs");
            }
            if (auto valid = validateClusters(graph, resolve, scene, inputs.clusters, inputs.ids); !valid)
                return valid;
            if (auto valid = validateShadows(graph, inputs); !valid)
                return valid;
            if (auto valid = validateSampledResources(graph, scene, inputs); !valid)
                return valid;
            const auto tracked = scene.trackedBuffers();
            for (size_t i = 0; i < tracked.size(); ++i)
            {
                auto imported = graph.getBufferExpected(inputs.sceneBuffers[i]);
                if (!imported || *imported != tracked[i])
                {
                    return std::unexpected("Visibility graph must import all exact scene buffers");
                }
            }
            return {};
        }
    } // namespace

    std::expected<void, std::string> addVisibilityRasterGraphPass(RenderGraph&                     graph,
                                                                  const VisibilityRasterPipeline&  raster,
                                                                  const VisibilityResolvePipeline& resolve,
                                                                  VisibilitySceneBindings&         scene,
                                                                  std::string                      name,
                                                                  VisibilityGraphInputs            inputs)
    {
        if (raster.colorInput() || resolve.colorInput())
        {
            return std::unexpected("Split visibility pass requires ordinary graphics pipelines");
        }
        if (auto valid = validateInputs(graph, raster, resolve, scene, inputs); !valid)
        {
            return valid;
        }
        auto&                         device = graph.device();
        auto                          state  = std::make_shared<VisibilityGraphInputs>(std::move(inputs));
        std::vector<RenderGraph::Use> rasterUses {RenderGraph::Use {state->ids, Usage::eColorWrite, true},
                                                  RenderGraph::Use {state->depth, Usage::eDepthWrite, true},
                                                  RenderGraph::Use {state->sceneBuffers[0], Usage::eStorageRead},
                                                  RenderGraph::Use {state->sceneBuffers[1], Usage::eStorageRead},
                                                  RenderGraph::Use {state->sceneBuffers[2], Usage::eStorageRead}};
        appendSampledResources(rasterUses, *state, false);
        auto rasterPass = graph.addPassExpected(
            name + ".raster",
            rasterUses,
            [&device, &raster, state](VriCommandBuffer* cmd, RenderGraph& active) -> std::expected<void, std::string>
            {
                auto ids      = active.getTextureExpected(state->ids);
                auto depth    = active.getTextureExpected(state->depth);
                auto vertices = active.getBufferExpected(state->sceneBuffers[0]);
                auto indices  = active.getBufferExpected(state->sceneBuffers[1]);
                if (!ids || !depth || !vertices || !indices)
                {
                    return std::unexpected("Visibility raster graph resource is unavailable");
                }
                return recordVisibilityRaster(device,
                                              raster,
                                              cmd,
                                              **ids,
                                              **depth,
                                              **vertices,
                                              **indices,
                                              state->frameSet,
                                              state->materialSets,
                                              state->dynamicDraws ? *state->dynamicDraws : state->draws);
            });
        return rasterPass;
    }

    std::expected<void, std::string> addVisibilityColorInputGraphPass(RenderGraph&                     graph,
                                                                      const VisibilityRasterPipeline&  raster,
                                                                      const VisibilityResolvePipeline& resolve,
                                                                      VisibilitySceneBindings&         scene,
                                                                      std::string                      name,
                                                                      VisibilityGraphInputs            inputs)
    {
        const auto* local = raster.colorInput();
        if (!local || local != resolve.colorInput() || !local->target())
        {
            return std::unexpected("Visibility color input requires two pipelines for the same target");
        }
        if (auto valid = validateInputs(graph, raster, resolve, scene, inputs); !valid)
        {
            return valid;
        }
        auto&                         device = graph.device();
        std::vector<RenderGraph::Use> uses {{inputs.ids, Usage::eColorWrite, true},
                                            {inputs.depth, Usage::eDepthWrite, true},
                                            {inputs.hdr, Usage::eColorWrite, true}};
        for (auto buffer : inputs.sceneBuffers)
        {
            uses.push_back({buffer, Usage::eStorageRead});
        }
        if (resolve.clustered())
        {
            uses.push_back({inputs.clusters, Usage::eStorageRead});
        }
        appendSampledResources(uses, inputs, true);
        return graph.addPassExpected(
            name + ".color_input",
            uses,
            [&device, &raster, &resolve, &scene, local, inputs = std::move(inputs)](
                VriCommandBuffer* cmd,
                RenderGraph&      active) -> std::expected<void, std::string>
            {
                auto ids      = active.getTextureExpected(inputs.ids);
                auto depth    = active.getTextureExpected(inputs.depth);
                auto hdr      = active.getTextureExpected(inputs.hdr);
                auto vertices = active.getBufferExpected(inputs.sceneBuffers[0]);
                auto indices  = active.getBufferExpected(inputs.sceneBuffers[1]);
                if (!ids || !depth || !hdr || !vertices || !indices || !local->matches(**ids, **depth, **hdr) ||
                    !((*hdr)->desc.usage & VriTextureUsage_ShaderResourceStorage))
                {
                    return std::unexpected("Visibility color input graph resources do not match its target");
                }
                auto hdrView = (*hdr)->viewExpected();
                if (!hdrView)
                {
                    return std::unexpected(hdrView.error());
                }
                const std::array<const VriDescriptor*, 3> auxiliary {*hdrView, inputs.demand, inputs.repair};
                if (auto rendered = recordVisibilityRaster(device,
                                                           raster,
                                                           cmd,
                                                           **ids,
                                                           **depth,
                                                           **vertices,
                                                           **indices,
                                                           inputs.frameSet,
                                                           inputs.materialSets,
                                                           inputs.dynamicDraws ? *inputs.dynamicDraws : inputs.draws);
                    !rendered)
                {
                    return rendered;
                }
                return recordVisibilityResolve(device, resolve, scene, cmd, **ids, **hdr, inputs.lighting, auxiliary);
            });
    }

    std::expected<void, std::string> addVisibilityGraphPasses(RenderGraph&                     graph,
                                                              const VisibilityRasterPipeline&  raster,
                                                              const VisibilityResolvePipeline& resolve,
                                                              VisibilitySceneBindings&         scene,
                                                              std::string                      name,
                                                              VisibilityGraphInputs            inputs)
    {
        if (auto added = addVisibilityRasterGraphPass(graph, raster, resolve, scene, name, inputs); !added)
        {
            return added;
        }
        return addVisibilityShadingGraphPass(graph, raster, resolve, scene, std::move(name), std::move(inputs));
    }

    std::expected<void, std::string> addVisibilityShadingGraphPass(RenderGraph&                     graph,
                                                                   const VisibilityRasterPipeline&  raster,
                                                                   const VisibilityResolvePipeline& resolve,
                                                                   VisibilitySceneBindings&         scene,
                                                                   std::string                      name,
                                                                   VisibilityGraphInputs            inputs)
    {
        if (raster.colorInput() || resolve.colorInput())
        {
            return std::unexpected("Split visibility pass requires ordinary graphics pipelines");
        }
        if (auto valid = validateInputs(graph, raster, resolve, scene, inputs); !valid)
        {
            return valid;
        }
        auto&                         device = graph.device();
        auto                          state  = std::make_shared<VisibilityGraphInputs>(std::move(inputs));
        std::vector<RenderGraph::Use> resolveUses {RenderGraph::Use {state->ids, Usage::eSampled},
                                                   RenderGraph::Use {state->hdr, Usage::eColorWrite, true},
                                                   RenderGraph::Use {state->sceneBuffers[0], Usage::eStorageRead},
                                                   RenderGraph::Use {state->sceneBuffers[1], Usage::eStorageRead},
                                                   RenderGraph::Use {state->sceneBuffers[2], Usage::eStorageRead},
                                                   RenderGraph::Use {state->sceneBuffers[3], Usage::eStorageRead},
                                                   RenderGraph::Use {state->sceneBuffers[4], Usage::eStorageRead}};
        if (resolve.clustered())
            resolveUses.push_back({state->clusters, Usage::eStorageRead});
        appendSampledResources(resolveUses, *state, true);
        return graph.addPassExpected(
            name + ".resolve",
            resolveUses,
            [&device, &resolve, &scene, state](VriCommandBuffer* cmd,
                                               RenderGraph&      active) -> std::expected<void, std::string>
            {
                auto ids = active.getTextureExpected(state->ids);
                auto hdr = active.getTextureExpected(state->hdr);
                if (!ids || !hdr)
                {
                    return std::unexpected("Visibility resolve graph resource is unavailable");
                }
                if (!((*hdr)->desc.usage & VriTextureUsage_ShaderResourceStorage))
                {
                    return std::unexpected("Visibility graph HDR requires storage usage for its descriptor");
                }
                auto hdrView = (*hdr)->viewExpected();
                if (!hdrView)
                {
                    return std::unexpected(hdrView.error());
                }
                const std::array<const VriDescriptor*, 3> auxiliary {*hdrView, state->demand, state->repair};
                return recordVisibilityResolve(device, resolve, scene, cmd, **ids, **hdr, state->lighting, auxiliary);
            });
    }

    std::expected<void, std::string> addVisibilityHoleShadingPass(RenderGraph&                     graph,
                                                                  const VisibilityResolvePipeline& pipeline,
                                                                  VisibilitySceneBindings&         scene,
                                                                  std::string                      name,
                                                                  VisibilityGraphInputs            inputs,
                                                                  RenderGraph::Resource            holes)
    {
        auto& device = graph.device();
        if (!pipeline.belongsTo(device) || pipeline.mode() != VisibilityResolveMode::eHoles ||
            !scene.belongsTo(device, pipeline) || !inputs.demand || !graph.ownsResource(inputs.ids) ||
            !graph.ownsResource(inputs.hdr) || !graph.ownsResource(holes) ||
            std::ranges::any_of(inputs.lighting,
                                [](const auto* view)
                                {
                                    return !view;
                                }))
        {
            return std::unexpected("Invalid masked visibility graph inputs");
        }
        if (auto valid = validateClusters(graph, pipeline, scene, inputs.clusters, inputs.ids); !valid)
            return valid;
        if (auto valid = validateShadows(graph, inputs); !valid)
            return valid;
        if (auto valid = validateSampledResources(graph, scene, inputs); !valid)
            return valid;
        const auto tracked = scene.trackedBuffers();
        for (size_t i = 0; i < tracked.size(); ++i)
        {
            auto imported = graph.getBufferExpected(inputs.sceneBuffers[i]);
            if (!imported || *imported != tracked[i])
            {
                return std::unexpected("Masked visibility graph must import exact scene buffers");
            }
        }
        std::vector<RenderGraph::Use> uses {RenderGraph::Use {inputs.ids, Usage::eSampled},
                                            RenderGraph::Use {holes, Usage::eSampled},
                                            RenderGraph::Use {inputs.hdr, Usage::eStorageWrite, true},
                                            RenderGraph::Use {inputs.sceneBuffers[0], Usage::eStorageRead},
                                            RenderGraph::Use {inputs.sceneBuffers[1], Usage::eStorageRead},
                                            RenderGraph::Use {inputs.sceneBuffers[2], Usage::eStorageRead},
                                            RenderGraph::Use {inputs.sceneBuffers[3], Usage::eStorageRead},
                                            RenderGraph::Use {inputs.sceneBuffers[4], Usage::eStorageRead}};
        if (pipeline.clustered())
            uses.push_back({inputs.clusters, Usage::eStorageRead});
        appendSampledResources(uses, inputs, true);
        return graph.addPassExpected(std::move(name),
                                     uses,
                                     [&device, &pipeline, &scene, inputs = std::move(inputs), holes](
                                         VriCommandBuffer* cmd,
                                         RenderGraph&      active) -> std::expected<void, std::string>
                                     {
                                         auto ids  = active.getTextureExpected(inputs.ids);
                                         auto hdr  = active.getTextureExpected(inputs.hdr);
                                         auto mask = active.getTextureExpected(holes);
                                         if (!ids || !hdr || !mask)
                                         {
                                             return std::unexpected("Masked visibility graph resource is unavailable");
                                         }
                                         return recordVisibilityHoleShading(device,
                                                                            pipeline,
                                                                            scene,
                                                                            cmd,
                                                                            **ids,
                                                                            **hdr,
                                                                            **mask,
                                                                            inputs.lighting,
                                                                            inputs.demand);
                                     });
    }

    std::expected<void, std::string> addVisibilityDemandShadingPass(RenderGraph&                     graph,
                                                                    const VisibilityResolvePipeline& pipeline,
                                                                    VisibilitySceneBindings&         scene,
                                                                    std::string                      name,
                                                                    VisibilityGraphInputs            inputs,
                                                                    RenderGraph::Resource            demand)
    {
        auto& device = graph.device();
        if (!pipeline.belongsTo(device) || pipeline.mode() != VisibilityResolveMode::eDemand ||
            !scene.belongsTo(device, pipeline) || !inputs.repair || !graph.ownsResource(inputs.ids) ||
            !graph.ownsResource(inputs.hdr) || !graph.ownsResource(demand) ||
            std::ranges::any_of(inputs.lighting,
                                [](const auto* view)
                                {
                                    return !view;
                                }))
        {
            return std::unexpected("Invalid demand visibility graph inputs");
        }
        if (auto valid = validateClusters(graph, pipeline, scene, inputs.clusters, inputs.ids); !valid)
            return valid;
        if (auto valid = validateShadows(graph, inputs); !valid)
            return valid;
        if (auto valid = validateSampledResources(graph, scene, inputs); !valid)
            return valid;
        const auto tracked = scene.trackedBuffers();
        for (size_t i = 0; i < tracked.size(); ++i)
        {
            auto imported = graph.getBufferExpected(inputs.sceneBuffers[i]);
            if (!imported || *imported != tracked[i])
            {
                return std::unexpected("Demand visibility graph must import exact scene buffers");
            }
        }
        std::vector<RenderGraph::Use> uses {RenderGraph::Use {inputs.ids, Usage::eSampled},
                                            RenderGraph::Use {demand, Usage::eSampled},
                                            RenderGraph::Use {inputs.hdr, Usage::eStorageWrite, true},
                                            RenderGraph::Use {inputs.sceneBuffers[0], Usage::eStorageRead},
                                            RenderGraph::Use {inputs.sceneBuffers[1], Usage::eStorageRead},
                                            RenderGraph::Use {inputs.sceneBuffers[2], Usage::eStorageRead},
                                            RenderGraph::Use {inputs.sceneBuffers[3], Usage::eStorageRead},
                                            RenderGraph::Use {inputs.sceneBuffers[4], Usage::eStorageRead}};
        if (pipeline.clustered())
            uses.push_back({inputs.clusters, Usage::eStorageRead});
        appendSampledResources(uses, inputs, true);
        return graph.addPassExpected(std::move(name),
                                     uses,
                                     [&device, &pipeline, &scene, inputs = std::move(inputs), demand](
                                         VriCommandBuffer* cmd,
                                         RenderGraph&      active) -> std::expected<void, std::string>
                                     {
                                         auto ids       = active.getTextureExpected(inputs.ids);
                                         auto hdr       = active.getTextureExpected(inputs.hdr);
                                         auto requested = active.getTextureExpected(demand);
                                         if (!ids || !hdr || !requested)
                                         {
                                             return std::unexpected("Demand visibility graph resource is unavailable");
                                         }
                                         return recordVisibilityDemandShading(device,
                                                                              pipeline,
                                                                              scene,
                                                                              cmd,
                                                                              **ids,
                                                                              **hdr,
                                                                              **requested,
                                                                              inputs.lighting,
                                                                              inputs.repair);
                                     });
    }

    std::expected<void, std::string> addStereoVisibilityGraphPasses(RenderGraph&                            graph,
                                                                    const VisibilityRasterPipeline&         raster,
                                                                    const VisibilityResolvePipeline&        resolve,
                                                                    std::array<VisibilitySceneBindings*, 2> scenes,
                                                                    std::string                             name,
                                                                    std::array<VisibilityGraphInputs, 2>    inputs)
    {
        if (!scenes[0] || !scenes[1] || scenes[0] == scenes[1] || !inputs[0].frameSet ||
            inputs[0].frameSet == inputs[1].frameSet || !inputs[0].lighting[0] ||
            inputs[0].lighting[0] == inputs[1].lighting[0])
        {
            return std::unexpected("Stereo visibility requires independent per-eye bindings and frame descriptors");
        }
        for (size_t eye = 0; eye < inputs.size(); ++eye)
        {
            if (auto valid = validateInputs(graph, raster, resolve, *scenes[eye], inputs[eye]); !valid)
            {
                return valid;
            }
        }
        const std::array outputs {inputs[0].ids,
                                  inputs[0].depth,
                                  inputs[0].hdr,
                                  inputs[1].ids,
                                  inputs[1].depth,
                                  inputs[1].hdr};
        for (size_t i = 0; i < outputs.size(); ++i)
        {
            for (size_t j = i + 1; j < outputs.size(); ++j)
            {
                if (outputs[i].index == outputs[j].index)
                {
                    return std::unexpected("Stereo visibility outputs must be distinct");
                }
            }
        }
        if (auto left =
                addVisibilityGraphPasses(graph, raster, resolve, *scenes[0], name + ".left", std::move(inputs[0]));
            !left)
        {
            return left;
        }
        return addVisibilityGraphPasses(graph, raster, resolve, *scenes[1], name + ".right", std::move(inputs[1]));
    }
} // namespace vultra
