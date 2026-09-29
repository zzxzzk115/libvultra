#include "openpbr_luts.hpp"

#include <vultra/function/renderer/builtin/builtin_renderer.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace vultra
{
    namespace
    {
        // Keep this layout in sync with builtin/shaders/resources/frame_block.slangh.
        struct FrameData
        {
            glm::mat4                viewProjection;
            glm::mat4                inverseViewProjection;
            glm::mat4                view;
            std::array<glm::mat4, 4> lightViewProjection;
            glm::vec4                cameraPosition;
            glm::vec4                lightDirection;
            glm::vec4                lightColor;
            glm::vec4                cascadeSplits;
            glm::vec4                cascadeWidths;
            glm::vec4                cascadeDepthRanges;
            glm::vec4                shadowParameters;
            glm::vec4                options;
            glm::vec4                cameraClip;
            glm::vec4                overrides;
        };

        struct MaterialData
        {
            glm::vec4  baseColor;
            glm::vec4  surface;
            glm::vec4  emission;
            glm::vec4  coat;
            glm::vec4  specular;
            glm::vec4  flags;
            glm::uvec4 meshlets; // First meshlet, count, frustum culling, debug colors.
        };

        static_assert(sizeof(FrameData) == 608);
        static_assert(sizeof(MaterialData) == 112);
        static_assert(sizeof(SceneVertex) == 64 && offsetof(SceneVertex, tangent) == 48);

        VriPipeline* createPipeline(Device&                        device,
                                    VriPipelineLayout*             layout,
                                    std::span<const VriShaderDesc> shaders,
                                    VriFormat                      format,
                                    bool                           mesh,
                                    bool                           depth)
        {
            VriVertexStreamDesc    stream {sizeof(SceneVertex), 0, VriVertexStepRate_PerVertex};
            VriVertexAttributeDesc attributes[5] {};
            attributes[0].format = VriFormat_RGB32_SFLOAT;
            attributes[0].offset = offsetof(SceneVertex, position);
            attributes[1].format = VriFormat_RGB32_SFLOAT;
            attributes[1].offset = offsetof(SceneVertex, normal);
            attributes[2].format = VriFormat_RG32_SFLOAT;
            attributes[2].offset = offsetof(SceneVertex, uv);
            attributes[3].format = VriFormat_RGBA32_SFLOAT;
            attributes[3].offset = offsetof(SceneVertex, color);
            attributes[4].format = VriFormat_RGBA32_SFLOAT;
            attributes[4].offset = offsetof(SceneVertex, tangent);
            VriColorAttachmentDesc color {};
            color.format         = format;
            color.colorWriteMask = VriColorWrite_RGBA;
            VriGraphicsPipelineDesc desc {};
            desc.pipelineLayout = layout;
            desc.shaders        = shaders.data();
            desc.shaderNum      = uint32_t(shaders.size());
            if (mesh)
            {
                desc.vertexInput = {attributes, 5, &stream, 1};
            }
            desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
            desc.rasterization.cullMode  = VriCullMode_None;
            desc.rasterization.lineWidth = 1;
            desc.multisample.sampleNum   = 1;
            if (format != VriFormat_Unknown)
            {
                desc.outputMerger.colors   = &color;
                desc.outputMerger.colorNum = 1;
            }
            if (depth)
            {
                desc.depthStencil.depthTest          = VRI_TRUE;
                desc.depthStencil.depthWrite         = VRI_TRUE;
                desc.depthStencil.depthCompareOp     = VriCompareOp_Less;
                desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
            }
            VriPipeline* result = nullptr;
            check(device.core.CreateGraphicsPipeline(device.handle, &desc, &result),
                  "Create built-in graphics pipeline");
            return result;
        }

        void setViewport(Device& device, VriCommandBuffer* cmd, Extent size)
        {
            const VriViewport viewport {0, 0, float(size.width), float(size.height), 0, 1};
            const VriRect     scissor {0, 0, size.width, size.height};
            device.core.CmdSetViewports(cmd, &viewport, 1);
            device.core.CmdSetScissors(cmd, &scissor, 1);
        }
    } // namespace

    BuiltinRenderer::BuiltinRenderer(Device&      device,
                                     GpuScene&    scene,
                                     Environment& environment,
                                     VriFormat    outputFormat) :
        m_Device(device),
        m_Scene(scene),
        m_Environment(environment),
        m_OutputFormat(outputFormat)
    {
        if (outputFormat != VriFormat_RGBA8_UNORM && outputFormat != VriFormat_BGRA8_UNORM &&
            outputFormat != VriFormat_RGBA16_SFLOAT)
        {
            throw std::invalid_argument("Tone mapping output must be display UNORM or linear RGBA16_SFLOAT");
        }
        try
        {
            const VriShaderStageFlags geometryStages =
                VriShaderStage_Vertex | (scene.meshlets ? VriShaderStage_Task | VriShaderStage_Mesh : 0);
            if (scene.meshlets)
            {
                check(vriGetInterface(device.handle, VRI_INTERFACE_MESHSHADER, sizeof(m_MeshApi), &m_MeshApi),
                      "Get meshlet draw interface (requires VriFeature_MeshShader)");
            }
            m_OpenPbrLuts = createOpenPbrLuts(device);
            m_FrameBuffer = std::make_unique<Buffer>(
                device,
                VriBufferDesc {sizeof(FrameData), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
            const VriBufferViewDesc bufferView {m_FrameBuffer->handle,
                                                VriDescriptorType_ConstantBuffer,
                                                VriFormat_Unknown,
                                                0,
                                                sizeof(FrameData)};
            check(device.core.CreateBufferView(device.handle, &bufferView, &m_FrameView), "Create frame uniform view");
            VriDescriptorRangeDesc frameRanges[12] {};
            for (uint32_t i = 0; i < 12; ++i)
            {
                frameRanges[i] = {i, 1, VriDescriptorType_Texture, geometryStages | VriShaderStage_Fragment};
            }
            frameRanges[0].descriptorType  = VriDescriptorType_ConstantBuffer;
            frameRanges[10].descriptorType = VriDescriptorType_Sampler;
            VriDescriptorRangeDesc materialRanges[kMaterialTextureCount + 1] {};
            for (uint32_t i = 0; i <= kMaterialTextureCount; ++i)
            {
                materialRanges[i] = {i, 1, VriDescriptorType_Texture, VriShaderStage_Fragment};
            }
            materialRanges[kMaterialTextureCount].descriptorType = VriDescriptorType_Sampler;
            VriDescriptorRangeDesc meshletRanges[4] {};
            for (uint32_t i = 0; i < 4; ++i)
            {
                meshletRanges[i] = {i,
                                    1,
                                    VriDescriptorType_StructuredBuffer,
                                    VriShaderStage_Task | VriShaderStage_Mesh};
            }
            VriDescriptorSetDesc  sets[3] {{0, frameRanges, 12},
                                           {1, materialRanges, kMaterialTextureCount + 1},
                                           {2, meshletRanges, 4}};
            VriPushConstantDesc   push {0, sizeof(MaterialData), geometryStages | VriShaderStage_Fragment};
            VriPipelineLayoutDesc layout {};
            layout.descriptorSets   = sets;
            layout.descriptorSetNum = scene.meshlets ? 3 : 2;
            layout.pushConstants    = &push;
            layout.pushConstantNum  = 1;
            layout.shaderStages     = push.shaderStages;
            check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout), "Create built-in layout");

            VriSamplerDesc sampler {};
            sampler.minFilter    = VriFilter_Linear;
            sampler.magFilter    = VriFilter_Linear;
            sampler.mipmapMode   = VriMipmapMode_Linear;
            sampler.addressModeU = VriAddressMode_Repeat;
            sampler.addressModeV = VriAddressMode_Repeat;
            sampler.addressModeW = VriAddressMode_Repeat;
            sampler.maxLod       = 32;
            check(device.core.CreateSampler(device.handle, &sampler, &m_MaterialSampler), "Create material sampler");
            sampler.addressModeV = VriAddressMode_ClampToEdge;
            sampler.addressModeW = VriAddressMode_ClampToEdge;
            check(device.core.CreateSampler(device.handle, &sampler, &m_EnvironmentSampler),
                  "Create environment sampler");
            const auto            count = uint32_t(scene.materials.size());
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum    = count + (scene.meshlets ? 2 : 1);
            pool.textureMaxNum          = count * kMaterialTextureCount + 10;
            pool.samplerMaxNum          = count + 1;
            pool.constantBufferMaxNum   = 1;
            pool.structuredBufferMaxNum = scene.meshlets ? 4 : 0;
            check(device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool), "Create built-in descriptor pool");
            check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_FrameSet, 1), "Allocate frame set");
            if (scene.meshlets)
            {
                check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 2, &m_MeshletSet, 1),
                      "Allocate meshlet set");
                VriDescriptorRangeUpdateDesc updates[4] {};
                const VriDescriptor*         views[4] {};
                for (uint32_t i = 0; i < 4; ++i)
                {
                    views[i]   = scene.meshlets->views[i];
                    updates[i] = {&views[i], 1};
                }
                device.core.UpdateDescriptorRanges(m_MeshletSet, 0, 4, updates);
            }
            m_MaterialSets.resize(count);
            check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 1, m_MaterialSets.data(), count),
                  "Allocate material sets");
            for (uint32_t material = 0; material < count; ++material)
            {
                const VriDescriptor*         descriptors[kMaterialTextureCount + 1] {};
                VriDescriptorRangeUpdateDesc updates[kMaterialTextureCount + 1] {};
                for (uint32_t slot = 0; slot <= kMaterialTextureCount; ++slot)
                {
                    descriptors[slot] = slot < kMaterialTextureCount ? scene.materialTextures[material][slot]->view() :
                                                                       m_MaterialSampler;
                    updates[slot].descriptors   = &descriptors[slot];
                    updates[slot].descriptorNum = 1;
                }
                device.core.UpdateDescriptorRanges(m_MaterialSets[material], 0, kMaterialTextureCount + 1, updates);
            }
            auto pipeline = [&](const char* file, bool mesh, bool depth, VriFormat format)
            {
                return std::make_unique<ShaderPipeline>(
                    device,
                    std::filesystem::path("builtin/shaders/passes") / file,
                    std::vector<ShaderEntry> {{mesh ? "vertexMain" : "screenVertex", VriShaderStage_Vertex},
                                              {"fragmentMain", VriShaderStage_Fragment}},
                    [this, mesh, depth, format](std::span<const VriShaderDesc> shaders)
                    {
                        return createPipeline(m_Device, m_Layout, shaders, format, mesh, depth);
                    },
                    "builtin/shaders",
                    std::vector<std::filesystem::path> {"builtin/shaders", "external"});
            };
            m_Shadow  = pipeline("shadow.slang", true, true, VriFormat_Unknown);
            m_Forward = pipeline("forward.slang", true, true, VriFormat_RGBA16_SFLOAT);
            if (scene.meshlets)
            {
                m_MeshForward = std::make_unique<ShaderPipeline>(
                    device,
                    "builtin/shaders/passes/meshlet_forward.slang",
                    std::vector<ShaderEntry> {{"taskMain", VriShaderStage_Task},
                                              {"meshMain", VriShaderStage_Mesh},
                                              {"fragmentMain", VriShaderStage_Fragment}},
                    [this](std::span<const VriShaderDesc> shaders)
                    {
                        return createPipeline(m_Device, m_Layout, shaders, VriFormat_RGBA16_SFLOAT, false, true);
                    },
                    "builtin/shaders",
                    std::vector<std::filesystem::path> {"builtin/shaders", "external"});
            }
            m_Skybox      = pipeline("skybox.slang", false, false, VriFormat_RGBA16_SFLOAT);
            m_ToneMapping = pipeline("tone_mapping.slang", false, false, m_OutputFormat);
        }
        catch (...)
        {
            release();
            throw;
        }
    }

    BuiltinRenderer::~BuiltinRenderer()
    {
        m_Device.waitIdle();
        release();
    }

    void BuiltinRenderer::release()
    {
        m_Shadow.reset();
        m_Forward.reset();
        m_MeshForward.reset();
        m_Skybox.reset();
        m_ToneMapping.reset();
        if (m_Pool)
        {
            m_Device.core.DestroyDescriptorPool(m_Pool);
        }
        for (auto* descriptor : {m_FrameView, m_MaterialSampler, m_EnvironmentSampler})
        {
            if (descriptor)
            {
                m_Device.core.DestroyDescriptor(descriptor);
            }
        }
        if (m_Layout)
        {
            m_Device.core.DestroyPipelineLayout(m_Layout);
        }
    }

    void BuiltinRenderer::drawScene(VriCommandBuffer* cmd, bool shadow, uint32_t cascade)
    {
        const bool meshShading = settings.meshShading && !shadow;
        m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
        auto* pipeline = shadow ? m_Shadow->handle() : m_Forward->handle();
        if (meshShading)
        {
            pipeline = m_MeshForward->handle();
            m_Device.core.CmdSetDescriptorSet(cmd, 2, m_MeshletSet);
        }
        m_Device.core.CmdSetPipeline(cmd, pipeline);
        m_Device.core.CmdSetDescriptorSet(cmd, 0, m_FrameSet);
        if (!meshShading)
        {
            const VriVertexBufferBinding vertices {m_Scene.vertices->handle, 0};
            m_Device.core.CmdSetVertexBuffers(cmd, 0, &vertices, 1);
            m_Device.core.CmdSetIndexBuffer(cmd, m_Scene.indices->handle, 0, VriIndexType_UInt32);
        }
        for (size_t i = 0; i < m_Scene.primitives.size(); ++i)
        {
            const auto& primitive  = m_Scene.primitives[i];
            const auto& material   = m_Scene.materials.at(primitive.material);
            float       normalMode = 0;
            if (material.normalImage >= 0)
            {
                const auto format = m_Scene.materialTextures[primitive.material][2]->desc.format;
                normalMode        = format == VriFormat_BC5_UNORM || format == VriFormat_RG8_UNORM ? 2.0f : 1.0f;
            }
            MaterialData parameters {
                material.baseColor,
                {material.baseMetalness, material.specularRoughness, material.specularIor, material.normalScale},
                {material.emissionColor * material.emissionLuminance, material.occlusionStrength},
                {material.coatWeight, material.coatRoughness, material.coatIor, material.specularWeight},
                {material.specularColor, material.baseDiffuseRoughness},
                {material.alphaCutoff, material.baseWeight, float(cascade), normalMode},
                {0, 0, 0, 0}};
            if (meshShading)
            {
                const auto range    = m_Scene.meshlets->primitives[i];
                parameters.meshlets = {range.first,
                                       range.count,
                                       settings.meshletCulling ? 1u : 0u,
                                       settings.meshletColors ? 1u : 0u};
            }
            m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
            m_Device.core.CmdSetDescriptorSet(cmd, 1, m_MaterialSets[primitive.material]);
            if (meshShading)
            {
                if (parameters.meshlets.y != 0)
                {
                    m_MeshApi.CmdDrawMeshTasks(cmd,
                                               (parameters.meshlets.y + kMeshletTaskSize - 1) / kMeshletTaskSize,
                                               1,
                                               1);
                }
            }
            else
            {
                const VriDrawIndexedDesc draw {primitive.indexCount, 1, primitive.firstIndex, 0, 0};
                m_Device.core.CmdDrawIndexed(cmd, &draw);
            }
        }
    }

    void BuiltinRenderer::drawFullscreen(VriCommandBuffer* cmd, VriPipeline* pipeline)
    {
        m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
        m_Device.core.CmdSetPipeline(cmd, pipeline);
        m_Device.core.CmdSetDescriptorSet(cmd, 0, m_FrameSet);
        const VriDrawDesc draw {3, 1, 0, 0};
        m_Device.core.CmdDraw(cmd, &draw);
    }

    BuiltinRenderer::ShadowMaps BuiltinRenderer::addShadowPasses(RenderGraph& graph)
    {
        ShadowMaps   maps;
        const Extent size {settings.shadowResolution, settings.shadowResolution};
        for (uint32_t cascade = 0; cascade < 4; ++cascade)
        {
            maps[cascade] = graph.createTexture("shadow_" + std::to_string(cascade), depthTexture(size));
            graph.addPass("Shadow " + std::to_string(cascade),
                          {{maps[cascade], Usage::eDepthWrite}},
                          [this, cascade, resource = maps[cascade], size](auto* cmd, auto& resources)
                          {
                              VriAttachmentDesc depth {};
                              depth.view                          = resources.getTexture(resource).view();
                              depth.loadOp                        = VriAttachmentLoadOp_Clear;
                              depth.storeOp                       = VriAttachmentStoreOp_Store;
                              depth.clearValue.depthStencil.depth = 1;
                              VriAttachmentsDesc attachments {};
                              attachments.depth      = &depth;
                              attachments.renderArea = {0, 0, size.width, size.height};
                              attachments.layerNum   = 1;
                              m_Device.core.CmdBeginRendering(cmd, &attachments);
                              setViewport(m_Device, cmd, size);
                              if (settings.shadowFilter != ShadowFilter::eDisabled)
                              {
                                  drawScene(cmd, true, cascade);
                              }
                              m_Device.core.CmdEndRendering(cmd);
                          });
        }
        return maps;
    }

    void BuiltinRenderer::addSkyboxPass(RenderGraph& graph, RenderGraph::Resource hdr)
    {
        graph.addPass("Skybox",
                      {{hdr, Usage::eColorWrite}},
                      [this, hdr](auto* cmd, auto& resources)
                      {
                          auto&       target = resources.getTexture(hdr);
                          const float clear[4] {0, 0, 0, 1};
                          beginColorPass(m_Device, cmd, target.view(), {target.desc.width, target.desc.height}, clear);
                          drawFullscreen(cmd, m_Skybox->handle());
                          m_Device.core.CmdEndRendering(cmd);
                      });
    }

    void BuiltinRenderer::addForwardPass(RenderGraph&          graph,
                                         RenderGraph::Resource hdr,
                                         RenderGraph::Resource depth,
                                         ShadowMaps            shadows)
    {
        graph.addPass("Forward OpenPBR",
                      {{hdr, Usage::eColorReadWrite},
                       {depth, Usage::eDepthWrite},
                       {shadows[0], Usage::eSampled},
                       {shadows[1], Usage::eSampled},
                       {shadows[2], Usage::eSampled},
                       {shadows[3], Usage::eSampled}},
                      [this, hdr, depth](auto* cmd, auto& resources)
                      {
                          auto&             target = resources.getTexture(hdr);
                          const Extent      size {target.desc.width, target.desc.height};
                          VriAttachmentDesc colorAttachment {};
                          colorAttachment.view    = target.view();
                          colorAttachment.loadOp  = VriAttachmentLoadOp_Load;
                          colorAttachment.storeOp = VriAttachmentStoreOp_Store;
                          VriAttachmentDesc depthAttachment {};
                          depthAttachment.view                          = resources.getTexture(depth).view();
                          depthAttachment.loadOp                        = VriAttachmentLoadOp_Clear;
                          depthAttachment.storeOp                       = VriAttachmentStoreOp_Store;
                          depthAttachment.clearValue.depthStencil.depth = 1;
                          VriAttachmentsDesc attachments {};
                          attachments.colors     = &colorAttachment;
                          attachments.colorNum   = 1;
                          attachments.depth      = &depthAttachment;
                          attachments.renderArea = {0, 0, size.width, size.height};
                          attachments.layerNum   = 1;
                          m_Device.core.CmdBeginRendering(cmd, &attachments);
                          setViewport(m_Device, cmd, size);
                          drawScene(cmd, false);
                          m_Device.core.CmdEndRendering(cmd);
                      });
    }

    RenderGraph::Resource
    BuiltinRenderer::addToneMappingPass(RenderGraph& graph, RenderGraph::Resource hdr, Extent size)
    {
        const auto color = graph.createTexture("display_color", colorTexture(size, m_OutputFormat));
        graph.addPass("Tone mapping",
                      {{hdr, Usage::eSampled}, {color, Usage::eColorWrite}},
                      [this, color, size](auto* cmd, auto& resources)
                      {
                          const float clear[4] {0, 0, 0, 1};
                          beginColorPass(m_Device, cmd, resources.getTexture(color).view(), size, clear);
                          drawFullscreen(cmd, m_ToneMapping->handle());
                          m_Device.core.CmdEndRendering(cmd);
                      });
        return color;
    }

    BuiltinRenderer::Outputs BuiltinRenderer::addPasses(RenderGraph& graph, Extent size)
    {
        Outputs outputs;
        outputs.shadows = addShadowPasses(graph);
        outputs.hdr     = graph.createTexture("scene_hdr", colorTexture(size, VriFormat_RGBA16_SFLOAT));
        outputs.depth   = graph.createTexture("scene_depth", depthTexture(size));
        addSkyboxPass(graph, outputs.hdr);
        addForwardPass(graph, outputs.hdr, outputs.depth, outputs.shadows);
        outputs.color = addToneMappingPass(graph, outputs.hdr, size);
        return outputs;
    }

    void BuiltinRenderer::prepare(const RenderCamera& camera, RenderGraph& graph, const Outputs& outputs)
    {
        if (settings.meshShading && !m_MeshForward)
        {
            throw std::logic_error("Mesh shading requires a GpuScene created with meshlets");
        }
        const auto& shadowDesc = graph.getTexture(outputs.shadows[0]).desc;
        const auto  cascades   = calculateCascades(camera,
                                                settings.directionToLight,
                                                m_Scene.center,
                                                m_Scene.radius,
                                                shadowDesc.width,
                                                settings.splitLambda);
        FrameData   data {};
        data.viewProjection        = camera.projection * camera.view;
        data.inverseViewProjection = glm::inverse(data.viewProjection);
        data.view                  = camera.view;
        data.lightViewProjection   = cascades.viewProjection;
        data.cameraPosition        = glm::inverse(camera.view)[3];
        data.lightDirection        = {glm::normalize(settings.directionToLight), settings.lightIntensity};
        data.lightColor            = {settings.lightColor, settings.environmentIntensity};
        data.cascadeSplits         = cascades.splits;
        data.cascadeWidths         = cascades.worldWidths;
        data.cascadeDepthRanges    = cascades.depthRanges;
        data.shadowParameters      = {settings.shadowBias,
                                      settings.normalBias,
                                      settings.sunAngularRadius,
                                      float(settings.shadowFilter)};
        data.options               = {settings.ibl ? 1.0f : 0.0f,
                        settings.skybox ? 1.0f : 0.0f,
                        settings.exposure,
                        float(m_Environment.specular->desc.mipNum - 1)};
        data.cameraClip = {camera.nearPlane,
                           camera.farPlane,
                           std::clamp(settings.cascadeBlend, 0.0f, 0.15f),
                           float(settings.debugMode)};
        data.overrides  = {settings.roughnessOverride,
                           settings.metalnessOverride,
                          m_OutputFormat == VriFormat_RGBA16_SFLOAT ? 0.0f : 1.0f,
                          settings.meshShading && settings.meshletColors ? 1.0f : 0.0f};
        auto* mapped    = m_Device.core.MapBuffer(m_FrameBuffer->handle, 0, sizeof(data));
        if (!mapped)
        {
            throw std::runtime_error("Map renderer frame data");
        }
        std::memcpy(mapped, &data, sizeof(data));
        m_Device.core.UnmapBuffer(m_FrameBuffer->handle);
        const VriDescriptor*         descriptors[12] {m_FrameView,
                                                      graph.getTexture(outputs.shadows[0]).view(),
                                                      graph.getTexture(outputs.shadows[1]).view(),
                                                      graph.getTexture(outputs.shadows[2]).view(),
                                                      graph.getTexture(outputs.shadows[3]).view(),
                                                      m_Environment.radiance->view(),
                                                      m_Environment.diffuse->view(),
                                                      m_Environment.specular->view(),
                                                      m_Environment.brdfLut->view(),
                                                      graph.getTexture(outputs.hdr).view(),
                                                      m_EnvironmentSampler,
                                                      m_OpenPbrLuts->view()};
        VriDescriptorRangeUpdateDesc updates[12] {};
        for (uint32_t i = 0; i < 12; ++i)
        {
            updates[i].descriptors   = &descriptors[i];
            updates[i].descriptorNum = 1;
        }
        m_Device.core.UpdateDescriptorRanges(m_FrameSet, 0, 12, updates);
    }

    void BuiltinRenderer::pollShaders()
    {
        m_Shadow->poll();
        m_Forward->poll();
        if (m_MeshForward)
        {
            m_MeshForward->poll();
        }
        m_Skybox->poll();
        m_ToneMapping->poll();
    }

    std::string BuiltinRenderer::diagnostics() const
    {
        return m_Shadow->diagnostics() + m_Forward->diagnostics() +
               (m_MeshForward ? m_MeshForward->diagnostics() : "") + m_Skybox->diagnostics() +
               m_ToneMapping->diagnostics();
    }
} // namespace vultra
