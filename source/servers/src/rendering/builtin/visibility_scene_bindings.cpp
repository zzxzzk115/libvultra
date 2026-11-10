#include <vultra/drivers/rhi/buffer_upload.hpp>
#include <vultra/servers/rendering/builtin/visibility_graph.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace vultra
{
    namespace
    {
        struct Material
        {
            glm::vec4 baseColor, surface, emission, coat, specular, flags;
        };

        static_assert(sizeof(Material) == 96);
    } // namespace

    std::expected<std::unique_ptr<VisibilitySceneBindings>, std::string>
    VisibilitySceneBindings::create(Device&                          device,
                                    GpuScene&                        scene,
                                    const VisibilityResolvePipeline& pipeline,
                                    std::span<VriDescriptor* const>  samplers,
                                    Buffer*                          clusters)
    {
        if (!scene.belongsTo(device))
        {
            return std::unexpected("Visibility scene belongs to another device");
        }
        return create(device,
                      {scene.vertices.get(),
                       scene.indices.get(),
                       scene.transforms.get(),
                       scene.primitives,
                       scene.materials,
                       scene.materialTextures,
                       scene.samplers.size()},
                      pipeline,
                      samplers,
                      clusters);
    }

    std::expected<std::unique_ptr<VisibilitySceneBindings>, std::string>
    VisibilitySceneBindings::create(Device&                          device,
                                    VisibilitySceneInputs            scene,
                                    const VisibilityResolvePipeline& pipeline,
                                    std::span<VriDescriptor* const>  samplers,
                                    Buffer*                          clusters)
    {
        if (pipeline.clustered() != (clusters != nullptr) ||
            (clusters && (!clusters->belongsTo(device) || !clusters->handle ||
                          !(clusters->desc.usage & VriBufferUsage_StorageBuffer) || clusters->desc.size < 8 ||
                          clusters->desc.size % 8 != 0)) ||
            !pipeline.belongsTo(device) || !pipeline.layout() || !pipeline.handle() || !scene.vertices ||
            !scene.indices || !scene.transforms || !scene.vertices->belongsTo(device) ||
            !scene.indices->belongsTo(device) || !scene.transforms->belongsTo(device) || !scene.vertices->handle ||
            !scene.indices->handle || !scene.transforms->handle ||
            !(scene.vertices->desc.usage & VriBufferUsage_StorageBuffer) ||
            !(scene.indices->desc.usage & VriBufferUsage_StorageBuffer) ||
            !(scene.transforms->desc.usage & VriBufferUsage_StorageBuffer) ||
            scene.vertices->desc.size != pipeline.vertexBufferSize() ||
            scene.indices->desc.size % sizeof(uint32_t) != 0 ||
            scene.primitives.size() > std::numeric_limits<uint32_t>::max() ||
            std::max<size_t>(scene.primitives.size(), 1) > scene.transforms->desc.size / 144 ||
            scene.materials.empty() || scene.materials.size() != pipeline.materialCount() ||
            scene.materialTextures.size() != scene.materials.size() ||
            scene.materials.size() > (std::numeric_limits<uint32_t>::max() - 13u) / kMaterialTextureCount ||
            scene.samplerCount == std::numeric_limits<size_t>::max() || samplers.size() != scene.samplerCount + 1)
        {
            return std::unexpected("Invalid visibility scene resources");
        }
        const auto              slots = uint32_t(scene.materials.size() * kMaterialTextureCount);
        std::vector<glm::uvec2> primitives;
        const auto*             caps = device.core.GetDeviceDesc(device.handle);
        if (!caps || scene.indices->desc.size > caps->bufferMaxSize ||
            scene.transforms->desc.size > caps->bufferMaxSize ||
            (scene.primitives.size() + 1) > caps->bufferMaxSize / sizeof(glm::uvec2) ||
            scene.materials.size() > caps->bufferMaxSize / sizeof(Material) ||
            (clusters && clusters->desc.size > caps->bufferMaxSize))
        {
            return std::unexpected("Visibility scene storage descriptor exceeds the device range limit");
        }
        primitives.reserve(scene.primitives.size() + 1);
        const uint64_t pageVertices = pipeline.vertexPageSize() / sizeof(SceneVertex);
        primitives.emplace_back(uint32_t(std::countr_zero(pageVertices)), uint32_t(pageVertices - 1));
        for (const auto& primitive : scene.primitives)
        {
            if (primitive.material >= scene.materials.size() || primitive.indexCount % 3 != 0 ||
                uint64_t(primitive.firstIndex) + primitive.indexCount > scene.indices->desc.size / sizeof(uint32_t))
            {
                return std::unexpected("Visibility primitive geometry or material is invalid");
            }
            primitives.emplace_back(primitive.firstIndex, primitive.material);
        }
        auto result = std::unique_ptr<VisibilitySceneBindings>(new VisibilitySceneBindings(device, scene, pipeline));
        auto primitiveBuffer = uploadBufferExpected(device,
                                                    std::as_bytes(std::span(primitives)),
                                                    VriBufferUsage_StorageBuffer,
                                                    {VriAccess_ShaderResourceRead, VriPipelineStage_FragmentShader},
                                                    sizeof(glm::uvec2));
        if (!primitiveBuffer)
        {
            return std::unexpected(primitiveBuffer.error());
        }
        result->m_Primitives = std::move(*primitiveBuffer);
        auto materialBuffer  = Buffer::create(device,
                                              {scene.materials.size() * sizeof(Material),
                                               sizeof(Material),
                                               VriBufferUsage_StorageBuffer,
                                               VriMemoryLocation_HostUpload});
        if (!materialBuffer)
        {
            return std::unexpected(materialBuffer.error());
        }
        result->m_Materials = std::move(*materialBuffer);
        result->m_VertexViews.resize(pipeline.vertexPageCount());
        for (size_t i = 0; i < result->m_VertexViews.size(); ++i)
        {
            const uint64_t          offset = i * pipeline.vertexPageSize();
            const VriBufferViewDesc view {scene.vertices->handle,
                                          VriDescriptorType_StructuredBuffer,
                                          VriFormat_Unknown,
                                          offset,
                                          std::min(pipeline.vertexPageSize(), scene.vertices->desc.size - offset)};
            const auto created = device.core.CreateBufferView(device.handle, &view, &result->m_VertexViews[i]);
            if (created != VriResult_Success || !result->m_VertexViews[i])
            {
                return std::unexpected("Create visibility vertex page failed: " + std::to_string(created));
            }
        }
        const std::array buffers {scene.indices,
                                  result->m_Primitives.get(),
                                  scene.transforms,
                                  result->m_Materials.get()};
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            const VriBufferViewDesc view {buffers[i]->handle,
                                          VriDescriptorType_StructuredBuffer,
                                          VriFormat_Unknown,
                                          0,
                                          buffers[i]->desc.size};
            const auto              status = device.core.CreateBufferView(device.handle, &view, &result->m_Views[i]);
            if (status != VriResult_Success || !result->m_Views[i])
            {
                return std::unexpected("Create visibility buffer view failed: " + std::to_string(status));
            }
        }
        result->m_Clusters = clusters;
        if (clusters)
        {
            const VriBufferViewDesc view {clusters->handle,
                                          VriDescriptorType_StructuredBuffer,
                                          VriFormat_Unknown,
                                          0,
                                          clusters->desc.size};
            const auto created = device.core.CreateBufferView(device.handle, &view, &result->m_ClusterView);
            if (created != VriResult_Success || !result->m_ClusterView)
                return std::unexpected("Create visibility cluster view failed: " + std::to_string(created));
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum    = pipeline.batched() ? 2 + uint32_t(scene.materials.size()) * 2 : 4;
        pool.constantBufferMaxNum   = 1;
        pool.storageTextureMaxNum   = 1;
        pool.structuredBufferMaxNum = pipeline.vertexPageCount() + (clusters ? 5u : 4u);
        pool.textureMaxNum          = slots + (pipeline.colorInput() ? 12 : 13);
        pool.inputAttachmentMaxNum  = pipeline.colorInput() ? 1 : 0;
        pool.samplerMaxNum          = slots + 1;
        auto status                 = device.core.CreateDescriptorPool(device.handle, &pool, &result->m_Pool);
        if (status != VriResult_Success || !result->m_Pool)
        {
            return std::unexpected("Create visibility descriptor pool failed: " + std::to_string(status));
        }
        const uint32_t commonSets = pipeline.batched() ? 2u : uint32_t(result->m_Sets.size());
        for (uint32_t i = 0; i < commonSets; ++i)
        {
            status = device.core.AllocateDescriptorSets(result->m_Pool, pipeline.layout(), i, &result->m_Sets[i], 1);
            if (status != VriResult_Success || !result->m_Sets[i])
            {
                return std::unexpected("Allocate visibility descriptor set failed: " + std::to_string(status));
            }
        }
        if (pipeline.batched())
        {
            result->m_MaterialSets.resize(scene.materials.size());
            for (auto& sets : result->m_MaterialSets)
            {
                for (uint32_t i = 0; i < sets.size(); ++i)
                {
                    status = device.core.AllocateDescriptorSets(result->m_Pool, pipeline.layout(), i + 2, &sets[i], 1);
                    if (status != VriResult_Success || !sets[i])
                    {
                        return std::unexpected("Allocate visibility material descriptor set failed: " +
                                               std::to_string(status));
                    }
                }
            }
        }
        std::vector<const VriDescriptor*> textures;
        std::vector<const VriDescriptor*> filters;
        textures.reserve(slots);
        filters.reserve(slots);
        for (size_t i = 0; i < scene.materials.size(); ++i)
        {
            const auto&      material = scene.materials[i];
            const std::array sources {material.baseColorTexture,
                                      material.metallicRoughnessTexture,
                                      material.normalTexture,
                                      material.occlusionTexture,
                                      material.emissionTexture,
                                      material.specularTexture,
                                      material.specularColorTexture};
            for (size_t slot = 0; slot < sources.size(); ++slot)
            {
                const auto index        = sources[slot].sampler;
                const auto samplerIndex = index < 0 ? size_t(0) : size_t(index) + 1;
                if (index < -1 || (index >= 0 && size_t(index) >= scene.samplerCount) ||
                    !scene.materialTextures[i][slot] || !scene.materialTextures[i][slot]->belongsTo(device) ||
                    !(scene.materialTextures[i][slot]->desc.usage & VriTextureUsage_ShaderResource) ||
                    !samplers[samplerIndex])
                {
                    return std::unexpected("Invalid visibility material texture or sampler");
                }
                auto view = scene.materialTextures[i][slot]->viewExpected();
                if (!view)
                {
                    return std::unexpected(view.error());
                }
                textures.push_back(*view);
                filters.push_back(samplers[samplerIndex]);
            }
        }
        if (pipeline.batched())
        {
            for (size_t i = 0; i < result->m_MaterialSets.size(); ++i)
            {
                const VriDescriptorRangeUpdateDesc textureUpdate {textures.data() + i * kMaterialTextureCount,
                                                                  kMaterialTextureCount};
                const VriDescriptorRangeUpdateDesc samplerUpdate {filters.data() + i * kMaterialTextureCount,
                                                                  kMaterialTextureCount};
                device.core.UpdateDescriptorRanges(result->m_MaterialSets[i][0], 0, 1, &textureUpdate);
                device.core.UpdateDescriptorRanges(result->m_MaterialSets[i][1], 0, 1, &samplerUpdate);
            }
        }
        else
        {
            const VriDescriptorRangeUpdateDesc textureUpdate {textures.data(), slots};
            const VriDescriptorRangeUpdateDesc samplerUpdate {filters.data(), slots};
            device.core.UpdateDescriptorRanges(result->m_Sets[2], 0, 1, &textureUpdate);
            device.core.UpdateDescriptorRanges(result->m_Sets[3], 0, 1, &samplerUpdate);
        }
        if (auto updated = result->updateMaterials(); !updated)
        {
            return std::unexpected(updated.error());
        }
        return result;
    }

    std::expected<void, std::string> VisibilitySceneBindings::updateMaterials()
    {
        if (m_Scene.materials.size() != m_Scene.materialTextures.size() ||
            m_Scene.materials.size() * sizeof(Material) != m_Materials->desc.size)
        {
            return std::unexpected("Visibility material table changed size; recreate bindings");
        }
        std::vector<Material> values;
        values.reserve(m_Scene.materials.size());
        bool uncoated = true;
        for (size_t i = 0; i < m_Scene.materials.size(); ++i)
        {
            const auto& source = m_Scene.materials[i];
            uncoated &= source.coatWeight == 0;
            if (!m_Scene.materialTextures[i][2])
            {
                return std::unexpected("Missing visibility normal texture");
            }
            float normal = 0;
            if (source.normalTexture.image >= 0)
            {
                normal = m_Scene.materialTextures[i][2]->isTwoChannel() ? 2.f : 1.f;
            }
            values.push_back({source.baseColor,
                              {source.baseMetalness, source.specularRoughness, source.specularIor, source.normalScale},
                              {source.emissionColor * source.emissionLuminance, source.occlusionStrength},
                              {source.coatWeight, source.coatRoughness, source.coatIor, source.specularWeight},
                              {source.specularColor, source.baseDiffuseRoughness},
                              {source.alphaCutoff, source.baseWeight, source.doubleSided ? 2.f : 0.f, normal}});
        }
        auto* mapped = m_Device.core.MapBuffer(m_Materials->handle, 0, m_Materials->desc.size);
        if (!mapped)
        {
            return std::unexpected("Map visibility material data failed");
        }
        std::memcpy(mapped, values.data(), m_Materials->desc.size);
        m_Device.core.UnmapBuffer(m_Materials->handle);
        m_Uncoated = uncoated;
        return {};
    }

    std::expected<void, std::string>
    VisibilitySceneBindings::updateFrame(const std::array<const VriDescriptor*, 12>& lighting,
                                         const std::array<const VriDescriptor*, 4>&  geometry)
    {
        if (std::ranges::any_of(lighting,
                                [](auto* view)
                                {
                                    return !view;
                                }) ||
            std::ranges::any_of(geometry,
                                [](auto* view)
                                {
                                    return !view;
                                }))
        {
            return std::unexpected("Visibility frame descriptors must all be valid");
        }
        std::array<VriDescriptorRangeUpdateDesc, 12> updates {};
        for (size_t i = 0; i < lighting.size(); ++i)
        {
            updates[i] = {&lighting[i], 1};
        }
        m_Device.core.UpdateDescriptorRanges(m_Sets[0], 0, uint32_t(lighting.size()), updates.data());
        std::array<const VriDescriptor*, 10> sceneViews {};
        for (size_t i = 0; i < m_Views.size(); ++i)
        {
            sceneViews[i + 1] = m_Views[i];
        }
        for (size_t i = 0; i < geometry.size(); ++i)
        {
            sceneViews[i + 5] = geometry[i];
        }
        sceneViews[9]               = m_ClusterView;
        const size_t sceneViewCount = m_Clusters ? 10 : 9;
        updates[0]                  = {m_VertexViews.data(), uint32_t(m_VertexViews.size())};
        for (size_t i = 1; i < sceneViewCount; ++i)
        {
            updates[i] = {&sceneViews[i], 1};
        }
        m_Device.core.UpdateDescriptorRanges(m_Sets[1], 0, uint32_t(sceneViewCount), updates.data());
        return {};
    }

    void VisibilitySceneBindings::transitionGeometry(VriCommandBuffer* commands, VriPipelineStageFlags stages)
    {
        const VriAccessStage read {VriAccess_ShaderResourceRead, stages};
        m_Scene.vertices->transition(commands, read);
        m_Scene.indices->transition(commands, read);
        m_Scene.transforms->transition(commands, read);
        m_Primitives->transition(commands, read);
        m_Materials->transition(commands, read);
    }

    std::expected<void, std::string> VisibilitySceneBindings::bind(VriCommandBuffer* commands, size_t material) const
    {
        if (!commands || (m_Pipeline.batched() && material >= m_MaterialSets.size()))
        {
            return std::unexpected("Invalid visibility material binding");
        }
        m_Device.core.CmdSetPipelineLayout(commands, m_Pipeline.layout());
        m_Device.core.CmdSetDescriptorSet(commands, 0, m_Sets[0]);
        m_Device.core.CmdSetDescriptorSet(commands, 1, m_Sets[1]);
        const auto& sets =
            m_Pipeline.batched() ? m_MaterialSets[material] : std::array<VriDescriptorSet*, 2> {m_Sets[2], m_Sets[3]};
        m_Device.core.CmdSetDescriptorSet(commands, 2, sets[0]);
        m_Device.core.CmdSetDescriptorSet(commands, 3, sets[1]);
        return {};
    }

    std::expected<void, std::string> recordVisibilityResolve(Device&                                     device,
                                                             const VisibilityResolvePipeline&            pipeline,
                                                             VisibilitySceneBindings&                    scene,
                                                             VriCommandBuffer*                           commands,
                                                             Texture&                                    ids,
                                                             Texture&                                    hdr,
                                                             const std::array<const VriDescriptor*, 12>& lighting,
                                                             const std::array<const VriDescriptor*, 3>&  auxiliary)
    {
        const auto* local  = pipeline.colorInput();
        const auto  record = [&]() -> std::expected<void, std::string>
        {
            if (!commands || pipeline.mode() != VisibilityResolveMode::eAll || !scene.belongsTo(device, pipeline) ||
                !ids.belongsTo(device) || !hdr.belongsTo(device) || ids.desc.format != VriFormat_RG32_UINT ||
                hdr.desc.format != VriFormat_RGBA16_SFLOAT || ids.desc.type != VriTextureType_2D ||
                hdr.desc.type != VriTextureType_2D || !ids.desc.width || !ids.desc.height ||
                ids.desc.width != hdr.desc.width || ids.desc.height != hdr.desc.height || ids.desc.layerNum != 1 ||
                hdr.desc.layerNum != 1 || ids.desc.sampleNum != 1 || hdr.desc.sampleNum != 1 ||
                !(ids.desc.usage & (local ? VriTextureUsage_InputAttachment : VriTextureUsage_ShaderResource)) ||
                !(hdr.desc.usage & VriTextureUsage_ColorAttachment))
            {
                return std::unexpected("Invalid visibility resolve resources");
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
            const std::array geometry {static_cast<const VriDescriptor*>(*idView),
                                       auxiliary[0],
                                       auxiliary[1],
                                       auxiliary[2]};
            if (auto updated = scene.updateFrame(lighting, geometry); !updated)
            {
                return updated;
            }
            if (!local)
            {
                scene.transitionGeometry(commands);
                ids.transition(
                    commands,
                    {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
                hdr.transition(commands,
                               {VriAccess_ColorAttachmentWrite,
                                VriLayout_ColorAttachment,
                                VriPipelineStage_ColorAttachmentOutput});
                VriAttachmentDesc color {};
                color.view    = *hdrView;
                color.loadOp  = VriAttachmentLoadOp_DontCare;
                color.storeOp = VriAttachmentStoreOp_Store;
                VriAttachmentsDesc attachments {};
                attachments.colors     = &color;
                attachments.colorNum   = 1;
                attachments.renderArea = {0, 0, hdr.desc.width, hdr.desc.height};
                attachments.layerNum   = 1;
                device.core.CmdBeginRendering(commands, &attachments);
            }
            const VriViewport viewport {0, 0, float(hdr.desc.width), float(hdr.desc.height), 0, 1};
            const VriRect     scissor {0, 0, hdr.desc.width, hdr.desc.height};
            device.core.CmdSetViewports(commands, &viewport, 1);
            device.core.CmdSetScissors(commands, &scissor, 1);
            device.core.CmdSetPipeline(commands, scene.resolvePipeline());
            const VriDrawDesc draw {3, 1, 0, 0};
            const size_t      draws = pipeline.batched() ? pipeline.materialCount() : 1;
            for (size_t material = 0; material < draws; ++material)
            {
                if (auto bound = scene.bind(commands, material); !bound)
                {
                    if (!local)
                    {
                        device.core.CmdEndRendering(commands);
                    }
                    return bound;
                }
                if (pipeline.batched() || local)
                {
                    const std::array<uint32_t, 4> push {uint32_t(material), 0, hdr.desc.width, hdr.desc.height};
                    device.core.CmdSetConstants(commands, 0, push.data(), sizeof(push));
                }
                device.core.CmdDraw(commands, &draw);
            }
            if (!local)
            {
                device.core.CmdEndRendering(commands);
            }
            return {};
        };
        if (!local)
        {
            return record();
        }
        if (!commands)
        {
            return std::unexpected("Visibility color input requires an active command buffer");
        }
        const auto& phase = local->commands();
        const auto  next  = phase.CmdResolve(commands);
        if (next != VriResult_Success)
        {
            return std::unexpected("Advance visibility color input failed: " + std::to_string(next));
        }
        const auto drawn = record();
        const auto end   = phase.CmdEnd(commands);
        if (!drawn)
        {
            return drawn;
        }
        if (end != VriResult_Success)
        {
            return std::unexpected("End visibility color input failed: " + std::to_string(end));
        }
        return {};
    }

    std::expected<void, std::string> recordVisibilityHoleShading(Device&                                     device,
                                                                 const VisibilityResolvePipeline&            pipeline,
                                                                 VisibilitySceneBindings&                    scene,
                                                                 VriCommandBuffer*                           commands,
                                                                 Texture&                                    ids,
                                                                 Texture&                                    hdr,
                                                                 Texture&                                    holes,
                                                                 const std::array<const VriDescriptor*, 12>& lighting,
                                                                 const VriDescriptor*                        demand)
    {
        if (!commands || pipeline.mode() != VisibilityResolveMode::eHoles || !scene.belongsTo(device, pipeline) ||
            !ids.belongsTo(device) || !hdr.belongsTo(device) || !holes.belongsTo(device) || !demand ||
            ids.desc.format != VriFormat_RG32_UINT || hdr.desc.format != VriFormat_RGBA16_SFLOAT ||
            holes.desc.format != VriFormat_R8_UINT || !ids.desc.width || !ids.desc.height ||
            ids.desc.width != hdr.desc.width || ids.desc.height != hdr.desc.height ||
            ids.desc.width != holes.desc.width || ids.desc.height != holes.desc.height ||
            ids.desc.type != VriTextureType_2D || hdr.desc.type != VriTextureType_2D ||
            holes.desc.type != VriTextureType_2D || ids.desc.layerNum != 1 || hdr.desc.layerNum != 1 ||
            holes.desc.layerNum != 1 || ids.desc.sampleNum != 1 || hdr.desc.sampleNum != 1 ||
            holes.desc.sampleNum != 1 || !(ids.desc.usage & VriTextureUsage_ShaderResource) ||
            !(holes.desc.usage & VriTextureUsage_ShaderResource) ||
            !(hdr.desc.usage & VriTextureUsage_ShaderResourceStorage))
        {
            return std::unexpected("Invalid masked visibility shading resources");
        }
        auto idView   = ids.viewExpected();
        auto hdrView  = hdr.viewExpected();
        auto holeView = holes.viewExpected();
        if (!idView || !hdrView || !holeView)
        {
            return std::unexpected("Create masked visibility texture views failed");
        }
        const std::array<const VriDescriptor*, 4> geometry {*idView, *hdrView, demand, *holeView};
        if (auto updated = scene.updateFrame(lighting, geometry); !updated)
        {
            return updated;
        }
        scene.transitionGeometry(commands, VriPipelineStage_ComputeShader);
        const VriAccessLayoutStage sampled {VriAccess_ShaderResourceRead,
                                            VriLayout_ShaderResource,
                                            VriPipelineStage_ComputeShader};
        ids.transition(commands, sampled);
        holes.transition(commands, sampled);
        const VriAccessLayoutStage write {VriAccess_ShaderResourceStorageWrite,
                                          VriLayout_General,
                                          VriPipelineStage_ComputeShader};
        hdr.transition(commands, write);
        device.core.CmdSetPipeline(commands, scene.resolvePipeline());
        const VriDispatchDesc dispatch {(ids.desc.width + 7) / 8, (ids.desc.height + 7) / 8, 1};
        const size_t          batches = pipeline.batched() ? pipeline.materialCount() : 1;
        for (size_t material = 0; material < batches; ++material)
        {
            if (material)
            {
                hdr.transition(commands, write);
            }
            if (auto bound = scene.bind(commands, material); !bound)
            {
                return bound;
            }
            if (pipeline.batched())
            {
                const std::array<uint32_t, 4> push {uint32_t(material), 0, 0, 0};
                device.core.CmdSetConstants(commands, 0, push.data(), sizeof(push));
            }
            device.core.CmdDispatch(commands, &dispatch);
        }
        return {};
    }

    std::expected<void, std::string> recordVisibilityDemandShading(Device&                                     device,
                                                                   const VisibilityResolvePipeline&            pipeline,
                                                                   VisibilitySceneBindings&                    scene,
                                                                   VriCommandBuffer*                           commands,
                                                                   Texture&                                    ids,
                                                                   Texture&                                    hdr,
                                                                   Texture&                                    demand,
                                                                   const std::array<const VriDescriptor*, 12>& lighting,
                                                                   const VriDescriptor*                        repair)
    {
        if (!commands || pipeline.mode() != VisibilityResolveMode::eDemand || !scene.belongsTo(device, pipeline) ||
            !ids.belongsTo(device) || !hdr.belongsTo(device) || !demand.belongsTo(device) || !repair ||
            ids.desc.format != VriFormat_RG32_UINT || hdr.desc.format != VriFormat_RGBA16_SFLOAT ||
            demand.desc.format != VriFormat_R32_UINT || !ids.desc.width || !ids.desc.height ||
            ids.desc.width != hdr.desc.width || ids.desc.height != hdr.desc.height ||
            demand.desc.width != (ids.desc.width / 2 + ids.desc.width % 2) ||
            demand.desc.height != (ids.desc.height / 2 + ids.desc.height % 2) || ids.desc.type != VriTextureType_2D ||
            hdr.desc.type != VriTextureType_2D || demand.desc.type != VriTextureType_2D || ids.desc.layerNum != 1 ||
            hdr.desc.layerNum != 1 || demand.desc.layerNum != 1 || ids.desc.mipNum != 1 || hdr.desc.mipNum != 1 ||
            demand.desc.mipNum != 1 || ids.desc.sampleNum != 1 || hdr.desc.sampleNum != 1 ||
            demand.desc.sampleNum != 1 || !(ids.desc.usage & VriTextureUsage_ShaderResource) ||
            !(demand.desc.usage & VriTextureUsage_ShaderResource) ||
            !(hdr.desc.usage & VriTextureUsage_ShaderResourceStorage))
        {
            return std::unexpected("Invalid demand visibility shading resources");
        }
        auto idView     = ids.viewExpected();
        auto hdrView    = hdr.viewExpected();
        auto demandView = demand.viewExpected();
        if (!idView || !hdrView || !demandView)
        {
            return std::unexpected("Create demand visibility texture views failed");
        }
        const std::array<const VriDescriptor*, 4> geometry {*idView, *hdrView, *demandView, repair};
        if (auto updated = scene.updateFrame(lighting, geometry); !updated)
        {
            return updated;
        }
        scene.transitionGeometry(commands, VriPipelineStage_ComputeShader);
        const VriAccessLayoutStage sampled {VriAccess_ShaderResourceRead,
                                            VriLayout_ShaderResource,
                                            VriPipelineStage_ComputeShader};
        ids.transition(commands, sampled);
        demand.transition(commands, sampled);
        hdr.transition(commands,
                       {VriAccess_ShaderResourceStorageWrite, VriLayout_General, VriPipelineStage_ComputeShader});
        device.core.CmdSetPipeline(commands, scene.resolvePipeline());
        if (auto bound = scene.bind(commands); !bound)
        {
            return bound;
        }
        const std::array<float, 4> push {-1, 0, 0, 0};
        device.core.CmdSetConstants(commands, 0, push.data(), sizeof(push));
        const VriDispatchDesc dispatch {(ids.desc.width + 15) / 16, (ids.desc.height + 15) / 16, 1};
        device.core.CmdDispatch(commands, &dispatch);
        return {};
    }

    VisibilitySceneBindings::~VisibilitySceneBindings()
    {
        m_Device.waitIdleForDestruction();
        if (m_Pool)
        {
            m_Device.core.DestroyDescriptorPool(m_Pool);
        }
        if (m_ClusterView)
            m_Device.core.DestroyDescriptor(m_ClusterView);
        for (auto* view : m_VertexViews)
        {
            if (view)
            {
                m_Device.core.DestroyDescriptor(view);
            }
        }
        for (auto* view : m_Views)
        {
            if (view)
            {
                m_Device.core.DestroyDescriptor(view);
            }
        }
    }
} // namespace vultra
