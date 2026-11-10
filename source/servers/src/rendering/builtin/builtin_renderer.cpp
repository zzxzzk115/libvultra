#include <vultra/assets/asset_source.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/builtin/frame_data.hpp>
#include <vultra/servers/rendering/builtin/openpbr_luts.hpp>
#include <vultra/servers/rendering/frustum.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>

namespace vultra
{
    namespace
    {
        using FrameData = BuiltinFrameData;

        struct MaterialData
        {
            glm::vec4  baseColor;
            glm::vec4  surface;
            glm::vec4  emission;
            glm::vec4  coat;
            glm::vec4  specular;
            glm::vec4  flags;
            glm::uvec4 meshlets; // First meshlet, count, frustum culling, debug colors.
            glm::uvec4 instance; // Primitive transform index.
        };

        static_assert(sizeof(MaterialData) == 128);
        static_assert(sizeof(SceneVertex) == 64 && offsetof(SceneVertex, tangent) == 48);

        constexpr std::array<VriFormat, 7> kGBufferFormats {VriFormat_RGBA32_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT,
                                                            VriFormat_RGBA16_SFLOAT};
        constexpr std::array               kGBufferNames {"gbuffer_position_metallic",
                                                          "gbuffer_normal_roughness",
                                                          "gbuffer_albedo_weight",
                                                          "gbuffer_emission_occlusion",
                                                          "gbuffer_specular",
                                                          "gbuffer_geometric_normal_ior",
                                                          "gbuffer_coat"};

        std::expected<VriPipeline*, std::string> createPipeline(Device&                        device,
                                                                VriPipelineLayout*             layout,
                                                                std::span<const VriShaderDesc> shaders,
                                                                std::span<const VriFormat>     formats,
                                                                bool                           mesh,
                                                                bool                           depth,
                                                                bool                           doubleSided,
                                                                bool                           mirrored   = false,
                                                                bool                           depthWrite = true,
                                                                VriCompareOp depthCompare = VriCompareOp_Less)
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
            std::array<VriColorAttachmentDesc, 4> colors {};
            if (formats.size() > colors.size())
            {
                return std::unexpected("Built-in pipeline supports at most four color attachments");
            }
            for (size_t i = 0; i < formats.size(); ++i)
            {
                colors[i].format         = formats[i];
                colors[i].colorWriteMask = VriColorWrite_RGBA;
            }
            VriGraphicsPipelineDesc desc {};
            desc.pipelineCache  = device.pipelineCache;
            desc.pipelineLayout = layout;
            desc.shaders        = shaders.data();
            desc.shaderNum      = uint32_t(shaders.size());
            if (mesh)
            {
                desc.vertexInput = {attributes, 5, &stream, 1};
            }
            desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
            desc.rasterization.cullMode  = doubleSided ? VriCullMode_None : VriCullMode_Back;
            desc.rasterization.frontFace = mirrored ? VriFrontFace_Clockwise : VriFrontFace_CounterClockwise;
            desc.rasterization.lineWidth = 1;
            desc.multisample.sampleNum   = 1;
            if (!formats.empty())
            {
                desc.outputMerger.colors   = colors.data();
                desc.outputMerger.colorNum = uint32_t(formats.size());
            }
            if (depth)
            {
                desc.depthStencil.depthTest          = VRI_TRUE;
                desc.depthStencil.depthWrite         = depthWrite ? VRI_TRUE : VRI_FALSE;
                desc.depthStencil.depthCompareOp     = depthCompare;
                desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
            }
            VriPipeline* result = nullptr;
            if (const auto status = device.core.CreateGraphicsPipeline(device.handle, &desc, &result);
                status != VriResult_Success)
            {
                if (result)
                {
                    device.core.DestroyPipeline(result);
                }
                return std::unexpected("Create built-in graphics pipeline failed: " + std::to_string(status));
            }
            return result;
        }

        std::expected<VriTextureDesc, std::string>
        rendererImageExpected(RenderGraph& graph, RenderGraph::Resource resource, VriFormat format, Extent size = {})
        {
            if (!graph.ownsResource(resource))
            {
                return std::unexpected("Built-in image belongs to another graph");
            }
            const auto  info = graph.resourceInfo(resource);
            const auto& desc = info.textureDesc;
            if (!info.isTexture || desc.format != format || desc.type != VriTextureType_2D || desc.mipNum != 1 ||
                desc.layerNum != 1 || desc.sampleNum != 1 ||
                (!size.empty() && (desc.width != size.width || desc.height != size.height)))
            {
                return std::unexpected("Built-in image format/shape differs from its pass contract");
            }
            return desc;
        }

        std::expected<VriDescriptor*, std::string> graphViewExpected(RenderGraph& graph, RenderGraph::Resource resource)
        {
            auto texture = graph.getTextureExpected(resource);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            return (*texture)->viewExpected();
        }

        void setViewport(Device& device, VriCommandBuffer* cmd, Extent size)
        {
            const VriViewport viewport {0, 0, float(size.width), float(size.height), 0, 1};
            const VriRect     scissor {0, 0, size.width, size.height};
            device.core.CmdSetViewports(cmd, &viewport, 1);
            device.core.CmdSetScissors(cmd, &scissor, 1);
        }
    } // namespace

    BuiltinRenderer::BuiltinRenderer(Device&            device,
                                     GpuScene&          scene,
                                     Environment&       environment,
                                     VriFormat          outputFormat,
                                     const AssetSource* shaders,
                                     Uninitialized) :
        m_Device(device),
        m_Scene(scene),
        m_Environment(environment),
        m_OutputFormat(outputFormat),
        m_ShaderAssets(shaders)
    {
    }

    std::expected<std::unique_ptr<BuiltinRenderer>, std::string> BuiltinRenderer::create(Device&      device,
                                                                                         GpuScene&    scene,
                                                                                         Environment& environment,
                                                                                         VriFormat    outputFormat,
                                                                                         const AssetSource* shaders)
    {
        auto renderer = std::unique_ptr<BuiltinRenderer>(
            new BuiltinRenderer(device, scene, environment, outputFormat, shaders, Uninitialized {}));
        if (auto initialized = renderer->initializeExpected(); !initialized)
        {
            return std::unexpected(initialized.error());
        }
        return renderer;
    }

    BuiltinRenderer::BuiltinRenderer(Device&            device,
                                     GpuScene&          scene,
                                     Environment&       environment,
                                     VriFormat          outputFormat,
                                     const AssetSource* shaders) :
        BuiltinRenderer(device, scene, environment, outputFormat, shaders, Uninitialized {})
    {
        if (auto initialized = initializeExpected(); !initialized)
        {
            release();
            throw std::runtime_error(initialized.error()); // Legacy boundary.
        }
    }

    std::expected<void, std::string> BuiltinRenderer::initializeExpected()
    {
        auto&       device  = m_Device;
        auto&       scene   = m_Scene;
        const auto* shaders = m_ShaderAssets;
        if (m_OutputFormat != VriFormat_RGBA8_UNORM && m_OutputFormat != VriFormat_BGRA8_UNORM &&
            m_OutputFormat != VriFormat_RGBA16_SFLOAT)
        {
            return std::unexpected("Tone mapping output must be display UNORM or linear RGBA16_SFLOAT");
        }
        if (!scene.belongsTo(device) || !scene.vertices || !scene.indices || !scene.transforms ||
            !scene.vertices->belongsTo(device) || !scene.indices->belongsTo(device) ||
            !scene.transforms->belongsTo(device))
        {
            return std::unexpected("Built-in rendering requires scene buffers on its device");
        }
        if (scene.materials.empty() || scene.materials.size() > (UINT32_MAX - 17u) / kMaterialTextureCount ||
            scene.materialTextures.size() != scene.materials.size())
        {
            return std::unexpected("Built-in material tables exceed descriptor limits or have inconsistent sizes");
        }
        for (const auto* texture : {m_Environment.radiance.get(),
                                    m_Environment.diffuse.get(),
                                    m_Environment.specular.get(),
                                    m_Environment.brdfLut.get()})
        {
            if (!texture || !texture->belongsTo(device))
            {
                return std::unexpected("Built-in rendering requires a complete environment on its device");
            }
            if (!(device.core.GetFormatSupport(device.handle, texture->desc.format) & VriFormatSupport_LinearFilter))
            {
                return std::unexpected("Built-in environment textures require native linear filtering support");
            }
        }
        for (const auto& textures : scene.materialTextures)
        {
            for (const auto* texture : textures)
            {
                if (!texture || !texture->belongsTo(device))
                {
                    return std::unexpected("Built-in material textures must belong to its device");
                }
            }
        }
        const VriShaderStageFlags geometryStages =
            VriShaderStage_Vertex | (scene.meshlets ? VriShaderStage_Task | VriShaderStage_Mesh : 0);
        if (scene.meshlets)
        {
            if (const auto status =
                    vriGetInterface(device.handle, VRI_INTERFACE_MESHSHADER, sizeof(m_MeshApi), &m_MeshApi);
                status != VriResult_Success)
            {
                return std::unexpected("Get meshlet draw interface (requires VriFeature_MeshShader) failed: " +
                                       std::to_string(status));
            }
        }
        auto luts = createOpenPbrLutsExpected(device);
        if (!luts)
        {
            return std::unexpected(luts.error());
        }
        m_OpenPbrLuts = std::move(*luts);
        auto frameBuffer =
            Buffer::create(device, {sizeof(FrameData), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
        if (!frameBuffer)
        {
            return std::unexpected(frameBuffer.error());
        }
        m_FrameBuffer = std::move(*frameBuffer);
        const VriBufferViewDesc bufferView {m_FrameBuffer->handle,
                                            VriDescriptorType_ConstantBuffer,
                                            VriFormat_Unknown,
                                            0,
                                            sizeof(FrameData)};
        if (const auto status = device.core.CreateBufferView(device.handle, &bufferView, &m_FrameView);
            status != VriResult_Success || !m_FrameView)
        {
            return std::unexpected("Create frame uniform view failed: " + std::to_string(status));
        }
        const VriBufferViewDesc transformView {scene.transforms->handle,
                                               VriDescriptorType_StructuredBuffer,
                                               VriFormat_Unknown,
                                               0,
                                               scene.transforms->desc.size};
        if (const auto status = device.core.CreateBufferView(device.handle, &transformView, &m_TransformView);
            status != VriResult_Success || !m_TransformView)
        {
            return std::unexpected("Create primitive transform view failed: " + std::to_string(status));
        }
        VriDescriptorRangeDesc frameRanges[20] {};
        for (uint32_t i = 0; i < 20; ++i)
        {
            frameRanges[i] = {i, 1, VriDescriptorType_Texture, geometryStages | VriShaderStage_Fragment};
        }
        frameRanges[0].descriptorType  = VriDescriptorType_ConstantBuffer;
        frameRanges[10].descriptorType = VriDescriptorType_Sampler;
        frameRanges[19]                = {19, 1, VriDescriptorType_StructuredBuffer, geometryStages};
        VriDescriptorRangeDesc materialRanges[kMaterialTextureCount + 1] {};
        for (uint32_t i = 0; i <= kMaterialTextureCount; ++i)
        {
            materialRanges[i] = {i, 1, VriDescriptorType_Texture, VriShaderStage_Fragment};
        }
        materialRanges[kMaterialTextureCount].descriptorType = VriDescriptorType_Sampler;
        materialRanges[kMaterialTextureCount].descriptorNum  = kMaterialTextureCount;
        VriDescriptorRangeDesc meshletRanges[4] {};
        for (uint32_t i = 0; i < 4; ++i)
        {
            meshletRanges[i] = {i, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Task | VriShaderStage_Mesh};
        }
        VriDescriptorSetDesc  sets[3] {{0, frameRanges, 20},
                                       {1, materialRanges, kMaterialTextureCount + 1},
                                       {2, meshletRanges, 4}};
        VriPushConstantDesc   push {0, sizeof(MaterialData), geometryStages | VriShaderStage_Fragment};
        VriPipelineLayoutDesc layout {};
        layout.descriptorSets   = sets;
        layout.descriptorSetNum = scene.meshlets ? 3 : 2;
        layout.pushConstants    = &push;
        layout.pushConstantNum  = 1;
        layout.shaderStages     = push.shaderStages;
        if (const auto status = device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout);
            status != VriResult_Success || !m_Layout)
        {
            return std::unexpected("Create built-in layout failed: " + std::to_string(status));
        }

        VriSamplerDesc sampler {};
        sampler.minFilter    = VriFilter_Linear;
        sampler.magFilter    = VriFilter_Linear;
        sampler.mipmapMode   = VriMipmapMode_Linear;
        sampler.addressModeU = VriAddressMode_Repeat;
        sampler.addressModeV = VriAddressMode_Repeat;
        sampler.addressModeW = VriAddressMode_Repeat;
        sampler.maxLod       = 32;
        m_MaterialSamplers.resize(scene.samplers.size() + 1);
        for (size_t i = 0; i < m_MaterialSamplers.size(); ++i)
        {
            const auto& desc = i == 0 ? sampler : scene.samplers[i - 1];
            if (const auto status = device.core.CreateSampler(device.handle, &desc, &m_MaterialSamplers[i]);
                status != VriResult_Success || !m_MaterialSamplers[i])
            {
                return std::unexpected("Create material sampler failed: " + std::to_string(status));
            }
        }
        sampler.addressModeV = VriAddressMode_ClampToEdge;
        sampler.addressModeW = VriAddressMode_ClampToEdge;
        if (const auto status = device.core.CreateSampler(device.handle, &sampler, &m_EnvironmentSampler);
            status != VriResult_Success || !m_EnvironmentSampler)
        {
            return std::unexpected("Create environment sampler failed: " + std::to_string(status));
        }
        const auto count = uint32_t(scene.materials.size());
        m_ShaderMaterials.resize(count);
        m_ShaderPipelines.resize(count);
        m_CandidatePipelines.resize(count);
        m_SceneBuffers = {scene.vertices->handle, scene.indices->handle, scene.transforms->handle};
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum    = count + (scene.meshlets ? 2 : 1);
        pool.textureMaxNum          = count * kMaterialTextureCount + 17;
        pool.samplerMaxNum          = count * kMaterialTextureCount + 1;
        pool.constantBufferMaxNum   = 1;
        pool.structuredBufferMaxNum = 1 + (scene.meshlets ? 4 : 0);
        if (const auto status = device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool);
            status != VriResult_Success || !m_Pool)
        {
            return std::unexpected("Create built-in descriptor pool failed: " + std::to_string(status));
        }
        if (const auto status = device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_FrameSet, 1);
            status != VriResult_Success || !m_FrameSet)
        {
            return std::unexpected("Allocate frame set failed: " + std::to_string(status));
        }
        if (scene.meshlets)
        {
            if (const auto status = device.core.AllocateDescriptorSets(m_Pool, m_Layout, 2, &m_MeshletSet, 1);
                status != VriResult_Success || !m_MeshletSet)
            {
                return std::unexpected("Allocate meshlet set failed: " + std::to_string(status));
            }
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
        if (const auto status = device.core.AllocateDescriptorSets(m_Pool, m_Layout, 1, m_MaterialSets.data(), count);
            status != VriResult_Success)
        {
            return std::unexpected("Allocate material sets failed: " + std::to_string(status));
        }
        if (std::ranges::find(m_MaterialSets, nullptr) != m_MaterialSets.end())
        {
            return std::unexpected("Allocate material sets returned an empty handle");
        }
        for (uint32_t material = 0; material < count; ++material)
        {
            const auto&                  source = scene.materials[material];
            const MaterialTexture        slots[] {source.baseColorTexture,
                                                  source.metallicRoughnessTexture,
                                                  source.normalTexture,
                                                  source.occlusionTexture,
                                                  source.emissionTexture,
                                                  source.specularTexture,
                                                  source.specularColorTexture};
            const VriDescriptor*         textures[kMaterialTextureCount] {};
            const VriDescriptor*         samplers[kMaterialTextureCount] {};
            VriDescriptorRangeUpdateDesc updates[kMaterialTextureCount + 1] {};
            for (uint32_t slot = 0; slot < kMaterialTextureCount; ++slot)
            {
                if (slots[slot].sampler < -1 ||
                    (slots[slot].sampler >= 0 && size_t(slots[slot].sampler) >= scene.samplers.size()))
                {
                    return std::unexpected("Invalid material sampler index");
                }
                auto view = scene.materialTextures[material][slot]->viewExpected();
                if (!view)
                {
                    return std::unexpected(view.error());
                }
                textures[slot] = *view;
                samplers[slot] = m_MaterialSamplers[size_t(slots[slot].sampler + 1)];
                updates[slot]  = {&textures[slot], 1};
            }
            updates[kMaterialTextureCount] = {samplers, kMaterialTextureCount};
            device.core.UpdateDescriptorRanges(m_MaterialSets[material], 0, kMaterialTextureCount + 1, updates);
        }
        auto pipeline =
            [&](const char* file, bool mesh, bool depth, VriFormat format, bool doubleSided, bool mirrored = false)
        {
            return ShaderPipeline::create(
                device,
                std::filesystem::path("builtin/shaders/passes") / file,
                std::vector<ShaderEntry> {{mesh ? "vertexMain" : "screenVertex", VriShaderStage_Vertex},
                                          {"fragmentMain", VriShaderStage_Fragment}},
                [this, mesh, depth, format, doubleSided, mirrored](std::span<const VriShaderDesc> shaders)
                {
                    const std::array singleFormat {format};
                    return createPipeline(m_Device,
                                          m_Layout,
                                          shaders,
                                          format == VriFormat_Unknown ? std::span<const VriFormat> {} :
                                                                        std::span<const VriFormat> {singleFormat},
                                          mesh,
                                          depth,
                                          doubleSided,
                                          mirrored);
                },
                "builtin/shaders",
                std::vector<std::filesystem::path> {"builtin/shaders", "external"},
                shaders);
        };
        for (uint32_t variant = 0; variant < 4; ++variant)
        {
            const bool doubleSided = (variant & 1) != 0;
            const bool mirrored    = (variant & 2) != 0;
            auto       shadow      = pipeline("shadow.slang", true, true, VriFormat_Unknown, doubleSided, mirrored);
            if (!shadow)
            {
                return std::unexpected(shadow.error());
            }
            m_Shadow[variant] = std::move(*shadow);
            auto forward      = pipeline("forward.slang", true, true, VriFormat_RGBA16_SFLOAT, doubleSided, mirrored);
            if (!forward)
            {
                return std::unexpected(forward.error());
            }
            m_Forward[variant] = std::move(*forward);
            for (uint32_t stage = 0; stage < 2; ++stage)
            {
                auto& target  = stage == 0 ? m_GBufferBase[variant] : m_GBufferMaterial[variant];
                auto  created = ShaderPipeline::create(
                    device,
                    "builtin/shaders/passes/gbuffer.slang",
                    std::vector<ShaderEntry> {
                        {"vertexMain", VriShaderStage_Vertex},
                        {stage == 0 ? "fragmentBase" : "fragmentMaterial", VriShaderStage_Fragment}},
                    [this, stage, doubleSided, mirrored](std::span<const VriShaderDesc> shaders)
                    {
                        const auto formats =
                            stage == 0 ? std::span(kGBufferFormats).first(4) : std::span(kGBufferFormats).subspan(4);
                        return createPipeline(m_Device,
                                              m_Layout,
                                              shaders,
                                              formats,
                                              true,
                                              true,
                                              doubleSided,
                                              mirrored,
                                              stage == 0,
                                              stage == 0 ? VriCompareOp_Less : VriCompareOp_Equal);
                    },
                    "builtin/shaders",
                    std::vector<std::filesystem::path> {"builtin/shaders", "external"},
                    shaders);
                if (!created)
                {
                    return std::unexpected(created.error());
                }
                target = std::move(*created);
            }
            if (scene.meshlets)
            {
                auto created = ShaderPipeline::create(
                    device,
                    "builtin/shaders/passes/meshlet_forward.slang",
                    std::vector<ShaderEntry> {{"taskMain", VriShaderStage_Task},
                                              {"meshMain", VriShaderStage_Mesh},
                                              {"fragmentMain", VriShaderStage_Fragment}},
                    [this, doubleSided, mirrored](std::span<const VriShaderDesc> shaders)
                    {
                        return createPipeline(m_Device,
                                              m_Layout,
                                              shaders,
                                              std::array {VriFormat_RGBA16_SFLOAT},
                                              false,
                                              true,
                                              doubleSided,
                                              mirrored);
                    },
                    "builtin/shaders",
                    std::vector<std::filesystem::path> {"builtin/shaders", "external"},
                    shaders);
                if (!created)
                {
                    return std::unexpected(created.error());
                }
                m_MeshForward[variant] = std::move(*created);
            }
        }
        auto skybox = pipeline("skybox.slang", false, false, VriFormat_RGBA16_SFLOAT, true);
        if (!skybox)
        {
            return std::unexpected(skybox.error());
        }
        m_Skybox         = std::move(*skybox);
        auto toneMapping = ToneMappingPass::create(device, m_OutputFormat, shaders);
        if (!toneMapping)
        {
            return std::unexpected(toneMapping.error());
        }
        m_ToneMapping = std::move(*toneMapping);
        auto deferred = pipeline("deferred_lighting.slang", false, false, VriFormat_RGBA16_SFLOAT, true);
        if (!deferred)
        {
            return std::unexpected(deferred.error());
        }
        m_DeferredLighting = std::move(*deferred);
        m_Initialized      = true;
        return {};
    }

    BuiltinRenderer::~BuiltinRenderer()
    {
        if (m_Initialized)
        {
            m_Device.waitIdleForDestruction();
        }
        release();
    }

    void BuiltinRenderer::release()
    {
        for (uint32_t variant = 0; variant < 4; ++variant)
        {
            m_Shadow[variant].reset();
            m_Forward[variant].reset();
            m_ForwardOpaque[variant].reset();
            m_MeshForward[variant].reset();
            m_GBufferBase[variant].reset();
            m_GBufferMaterial[variant].reset();
        }
        m_Skybox.reset();
        m_ToneMapping.reset();
        m_DeferredLighting.reset();
        if (m_Pool)
        {
            m_Device.core.DestroyDescriptorPool(m_Pool);
        }
        for (auto* sampler : m_MaterialSamplers)
        {
            if (sampler)
            {
                m_Device.core.DestroyDescriptor(sampler);
            }
        }
        for (auto* descriptor : {m_FrameView, m_TransformView, m_EnvironmentSampler})
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

    std::expected<void, std::string>
    BuiltinRenderer::drawSceneExpected(VriCommandBuffer* cmd, GeometryPass pass, uint32_t cascade)
    {
        const bool meshShading = settings.meshShading && pass == GeometryPass::eForward;
        m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
        if (!meshShading)
        {
            const VriVertexBufferBinding vertices {m_Scene.vertices->handle, 0};
            m_Device.core.CmdSetVertexBuffers(cmd, 0, &vertices, 1);
            m_Device.core.CmdSetIndexBuffer(cmd, m_Scene.indices->handle, 0, VriIndexType_UInt32);
        }
        VriPipeline* activePipeline = nullptr;
        const auto&  visible = m_PreparedFrame.visiblePrimitives[pass == GeometryPass::eShadow ? cascade + 1 : 0];
        for (const uint32_t i : visible)
        {
            if (i >= m_Scene.primitives.size())
            {
                return std::unexpected("Prepared primitive index is stale");
            }
            const auto& primitive = m_Scene.primitives[i];
            if (primitive.material >= m_Scene.materials.size() || primitive.material >= m_ShaderMaterials.size())
            {
                return std::unexpected("Prepared primitive material index is stale");
            }
            const auto& material  = m_Scene.materials[primitive.material];
            auto        reflected = m_Scene.primitiveMirroredExpected(i);
            if (!reflected)
            {
                return std::unexpected(reflected.error());
            }
            if (auto* shader = m_ShaderMaterials[primitive.material])
            {
                const auto purpose  = meshShading ? 4u : uint32_t(pass);
                const auto mirrored = *reflected ? 1u : 0u;
                if (auto bound =
                        shader->bindExpected(cmd, m_ShaderPipelines[primitive.material][mirrored * 5 + purpose]);
                    !bound)
                {
                    return std::unexpected(bound.error());
                }
                const VriVertexBufferBinding vertices {m_Scene.vertices->handle, 0};
                m_Device.core.CmdSetVertexBuffers(cmd, 0, &vertices, 1);
                m_Device.core.CmdSetIndexBuffer(cmd, m_Scene.indices->handle, 0, VriIndexType_UInt32);

                struct GameDraw
                {
                    glm::uvec4 instance;
                    glm::uvec4 meshlets;
                } parameters {{uint32_t(i), cascade, 0, 0}, {0, 0, 0, 0}};

                if (meshShading)
                {
                    const auto range    = m_Scene.meshlets->primitives[i];
                    parameters.meshlets = {range.first, range.count, settings.meshletCulling ? 1u : 0u, 0};
                }
                m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
                if (meshShading)
                {
                    m_MeshApi.CmdDrawMeshTasks(cmd,
                                               (parameters.meshlets.y + kMeshletTaskSize - 1) / kMeshletTaskSize,
                                               1,
                                               1);
                }
                else
                {
                    const VriDrawIndexedDesc draw {primitive.indexCount, 1, primitive.firstIndex, 0, 0};
                    m_Device.core.CmdDrawIndexed(cmd, &draw);
                }
                // The next imported primitive restores its descriptors after binding its graphics pipeline.
                m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
                activePipeline = nullptr;
                continue;
            }
            const uint32_t variant  = (material.doubleSided ? 1u : 0u) + (*reflected ? 2u : 0u);
            VriPipeline* pipeline = nullptr;
            switch (pass)
            {
                case GeometryPass::eShadow:
                    pipeline = m_Shadow[variant]->handle();
                    break;
                case GeometryPass::eForward:
                    pipeline = m_ForwardOpaque[variant] && m_Scene.hasImmutableOpaqueAlpha(primitive.material) &&
                                       material.baseColor.a > 0 &&
                                       material.alphaCutoff < material.baseColor.a * 0.999f ?
                                   m_ForwardOpaque[variant]->handle() : m_Forward[variant]->handle();
                    break;
                case GeometryPass::eGBufferBase:
                    pipeline = m_GBufferBase[variant]->handle();
                    break;
                case GeometryPass::eGBufferMaterial:
                    pipeline = m_GBufferMaterial[variant]->handle();
                    break;
            }
            if (meshShading)
            {
                pipeline = m_MeshForward[variant]->handle();
            }
            if (pipeline != activePipeline)
            {
                m_Device.core.CmdSetPipeline(cmd, pipeline);
                // VRI binds descriptors at the current pipeline's bind point, which may previously be compute.
                m_Device.core.CmdSetDescriptorSet(cmd, 0, m_FrameSet);
                if (meshShading)
                {
                    m_Device.core.CmdSetDescriptorSet(cmd, 2, m_MeshletSet);
                }
                activePipeline = pipeline;
            }
            float normalMode = 0;
            if (material.normalTexture.image >= 0)
            {
                normalMode = m_Scene.materialTextures[primitive.material][2]->isTwoChannel() ? 2.0f : 1.0f;
            }
            MaterialData parameters {
                material.baseColor,
                {material.baseMetalness, material.specularRoughness, material.specularIor, material.normalScale},
                {material.emissionColor * material.emissionLuminance, material.occlusionStrength},
                {material.coatWeight, material.coatRoughness, material.coatIor, material.specularWeight},
                {material.specularColor, material.baseDiffuseRoughness},
                {material.alphaCutoff, material.baseWeight, float(cascade), normalMode},
                {0, 0, 0, 0},
                {uint32_t(i), 0, 0, 0}};
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
        return {};
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
        auto added = addShadowPassesExpected(graph);
        if (!added)
        {
            throw std::invalid_argument(added.error()); // Legacy boundary.
        }
        return *added;
    }

    void BuiltinRenderer::addSkyboxPass(RenderGraph& graph, RenderGraph::Resource hdr)
    {
        auto added = addSkyboxPassExpected(graph, hdr);
        if (!added)
        {
            throw std::invalid_argument(added.error()); // Legacy boundary.
        }
    }

    void BuiltinRenderer::addForwardPass(RenderGraph&          graph,
                                         RenderGraph::Resource hdr,
                                         RenderGraph::Resource depth,
                                         ShadowMaps            shadows)
    {
        auto added = addForwardPassExpected(graph, hdr, depth, shadows);
        if (!added)
        {
            throw std::invalid_argument(added.error()); // Legacy boundary.
        }
    }

    BuiltinRenderer::GBuffer
    BuiltinRenderer::addGBufferPasses(RenderGraph& graph, RenderGraph::Resource depth, Extent size)
    {
        auto added = addGBufferPassesExpected(graph, depth, size);
        if (!added)
        {
            throw std::invalid_argument(added.error()); // Legacy boundary.
        }
        return *added;
    }

    void BuiltinRenderer::addDeferredLightingPass(RenderGraph&          graph,
                                                  RenderGraph::Resource hdr,
                                                  GBuffer               gbuffer,
                                                  ShadowMaps            shadows)
    {
        auto added = addDeferredLightingPassExpected(graph, hdr, gbuffer, shadows);
        if (!added)
        {
            throw std::invalid_argument(added.error()); // Legacy boundary.
        }
    }

    BuiltinRenderer::Outputs BuiltinRenderer::addScenePasses(RenderGraph& graph, Extent size)
    {
        auto added = addScenePassesExpected(graph, size);
        if (!added)
        {
            throw std::invalid_argument(added.error()); // Legacy boundary.
        }
        return *added;
    }

    BuiltinRenderer::Outputs BuiltinRenderer::addPasses(RenderGraph& graph, Extent size)
    {
        auto added = addPassesExpected(graph, size);
        if (!added)
        {
            throw std::invalid_argument(added.error()); // Legacy boundary.
        }
        return *added;
    }

    std::expected<BuiltinRenderer::ShadowMaps, std::string> BuiltinRenderer::addShadowPassesExpected(RenderGraph& graph)
    {
        if (&graph.device() != &m_Device || settings.shadowResolution < 16)
        {
            return std::unexpected("Built-in shadows require their device and resolution of at least 16");
        }
        RenderGraph::AppendTransaction transaction(graph);
        ShadowMaps                     maps {};
        const Extent                   size {settings.shadowResolution, settings.shadowResolution};
        for (uint32_t cascade = 0; cascade < maps.size(); ++cascade)
        {
            auto desc = depthTexture(size);
            desc.usage |= VriTextureUsage_TransferSrc;
            auto created = graph.createTextureExpected("shadow_" + std::to_string(cascade), desc);
            if (!created)
            {
                return std::unexpected(created.error());
            }
            maps[cascade] = *created;
            if (auto preserved = graph.preserveContentsExpected(maps[cascade]); !preserved)
            {
                return std::unexpected(preserved.error());
            }
            const std::array uses {RenderGraph::Use {maps[cascade], Usage::eDepthWrite}};
            auto             added = graph.addPassExpected(
                "Shadow " + std::to_string(cascade),
                uses,
                [this, cascade, resource = maps[cascade], size](auto* cmd,
                                                                auto& resources) -> std::expected<void, std::string>
                {
                    if (auto ready = requirePreparedExpected(resources); !ready)
                    {
                        return ready;
                    }
                    const auto& key = m_PreparedFrame.shadowKeys[cascade];
                    if (m_PreparedFrame.cacheShadows && m_ShadowCache[cascade] && *m_ShadowCache[cascade] == key)
                    {
                        m_ShadowDrawn[cascade] = false;
                        return {};
                    }
                    auto view = graphViewExpected(resources, resource);
                    if (!view)
                    {
                        return std::unexpected(view.error());
                    }
                    m_ShadowCache[cascade].reset();
                    m_ShadowDrawn[cascade] = settings.shadowFilter != ShadowFilter::eDisabled;
                    VriAttachmentDesc depth {};
                    depth.view                          = *view;
                    depth.loadOp                        = VriAttachmentLoadOp_Clear;
                    depth.storeOp                       = VriAttachmentStoreOp_Store;
                    depth.clearValue.depthStencil.depth = 1;
                    VriAttachmentsDesc attachments {};
                    attachments.depth      = &depth;
                    attachments.renderArea = {0, 0, size.width, size.height};
                    attachments.layerNum   = 1;
                    m_Device.core.CmdBeginRendering(cmd, &attachments);
                    setViewport(m_Device, cmd, size);
                    std::expected<void, std::string> drawn;
                    if (settings.shadowFilter != ShadowFilter::eDisabled)
                    {
                        drawn = drawSceneExpected(cmd, GeometryPass::eShadow, cascade);
                    }
                    m_Device.core.CmdEndRendering(cmd);
                    if (!drawn)
                    {
                        return drawn;
                    }
                    if (m_PreparedFrame.cacheShadows)
                    {
                        m_RecordedShadows[cascade] = key;
                    }
                    return {};
                },
                false,
                [this, cascade]() -> std::expected<RenderGraph::RestorePassState, std::string>
                {
                    const auto cache    = m_ShadowCache[cascade];
                    const auto recorded = m_RecordedShadows[cascade];
                    const auto drawn    = m_ShadowDrawn[cascade];
                    return RenderGraph::RestorePassState(
                        [this, cascade, cache, recorded, drawn]() -> std::expected<void, std::string>
                        {
                            m_ShadowCache[cascade]     = cache;
                            m_RecordedShadows[cascade] = recorded;
                            m_ShadowDrawn[cascade]     = drawn;
                            return {};
                        });
                });
            if (!added)
            {
                return std::unexpected(added.error());
            }
        }
        transaction.commit();
        return maps;
    }

    std::expected<void, std::string> BuiltinRenderer::addSkyboxPassExpected(RenderGraph&          graph,
                                                                            RenderGraph::Resource hdr)
    {
        if (&graph.device() != &m_Device)
        {
            return std::unexpected("Built-in graph belongs to another device");
        }
        auto image = rendererImageExpected(graph, hdr, VriFormat_RGBA16_SFLOAT);
        if (!image)
        {
            return std::unexpected(image.error());
        }
        const Extent     size {image->width, image->height};
        const std::array uses {RenderGraph::Use {hdr, Usage::eColorWrite}};
        return graph.addPassExpected("Skybox",
                                     uses,
                                     [this, hdr, size](auto* cmd, auto& resources) -> std::expected<void, std::string>
                                     {
                                         if (auto ready = requirePreparedExpected(resources); !ready)
                                         {
                                             return ready;
                                         }
                                         auto view = graphViewExpected(resources, hdr);
                                         if (!view)
                                         {
                                             return std::unexpected(view.error());
                                         }
                                         const float clear[4] {0, 0, 0, 1};
                                         beginColorPass(m_Device, cmd, *view, size, clear);
                                         drawFullscreen(cmd, m_Skybox->handle());
                                         m_Device.core.CmdEndRendering(cmd);
                                         return {};
                                     });
    }

    std::expected<void, std::string> BuiltinRenderer::addForwardPassExpected(RenderGraph&          graph,
                                                                             RenderGraph::Resource hdr,
                                                                             RenderGraph::Resource depth,
                                                                             ShadowMaps            shadows)
    {
        if (&graph.device() != &m_Device || graph.aliasesTransients())
        {
            return std::unexpected("Built-in graph belongs to another device");
        }
        auto image = rendererImageExpected(graph, hdr, VriFormat_RGBA16_SFLOAT);
        if (!image)
        {
            return std::unexpected(image.error());
        }
        const Extent size {image->width, image->height};
        if (auto valid = rendererImageExpected(graph, depth, VriFormat_D32_SFLOAT, size); !valid)
        {
            return std::unexpected(valid.error());
        }
        for (const auto shadow : shadows)
        {
            if (auto valid = rendererImageExpected(graph, shadow, VriFormat_D32_SFLOAT); !valid)
            {
                return std::unexpected(valid.error());
            }
        }
        bool hasOpaque = false;
        for (uint32_t material = 0; material < m_Scene.materials.size(); ++material)
        {
            hasOpaque |= m_Scene.hasImmutableOpaqueAlpha(material);
        }
        bool opaqueShaderAvailable = true;
        if (m_ShaderAssets)
        {
            auto contains = m_ShaderAssets->containsExpected("builtin/shaders/passes/forward_opaque.vshaderc");
            if (!contains)
            {
                return std::unexpected(contains.error());
            }
            opaqueShaderAvailable = *contains;
        }
        if (hasOpaque && opaqueShaderAvailable && !m_ForwardOpaque[0])
        {
            std::array<std::unique_ptr<ShaderPipeline>, 4> candidates;
            for (uint32_t variant = 0; variant < candidates.size(); ++variant)
            {
                const bool doubleSided = (variant & 1) != 0;
                const bool mirrored    = (variant & 2) != 0;
                auto       created     = ShaderPipeline::create(
                    m_Device,
                    "builtin/shaders/passes/forward_opaque.slang",
                    {{"vertexMain", VriShaderStage_Vertex}, {"fragmentMain", VriShaderStage_Fragment}},
                    [this, doubleSided, mirrored](std::span<const VriShaderDesc> shaders)
                    {
                        const std::array formats {VriFormat_RGBA16_SFLOAT};
                        return createPipeline(m_Device, m_Layout, shaders, formats, true, true, doubleSided, mirrored);
                    },
                    "builtin/shaders",
                    {"builtin/shaders", "external"},
                    m_ShaderAssets);
                if (!created)
                {
                    return std::unexpected(created.error());
                }
                candidates[variant] = std::move(*created);
            }
            m_ForwardOpaque.swap(candidates);
        }
        const std::array uses {RenderGraph::Use {hdr, Usage::eColorReadWrite},
                               RenderGraph::Use {depth, Usage::eDepthWrite},
                               RenderGraph::Use {shadows[0], Usage::eSampled},
                               RenderGraph::Use {shadows[1], Usage::eSampled},
                               RenderGraph::Use {shadows[2], Usage::eSampled},
                               RenderGraph::Use {shadows[3], Usage::eSampled}};
        return graph.addPassExpected(
            "Forward OpenPBR",
            uses,
            [this, hdr, depth, size](auto* cmd, auto& resources) -> std::expected<void, std::string>
            {
                if (auto ready = requirePreparedExpected(resources); !ready)
                {
                    return ready;
                }
                auto color = graphViewExpected(resources, hdr);
                if (!color)
                {
                    return std::unexpected(color.error());
                }
                auto depthView = graphViewExpected(resources, depth);
                if (!depthView)
                {
                    return std::unexpected(depthView.error());
                }
                VriAttachmentDesc colorAttachment {};
                colorAttachment.view    = *color;
                colorAttachment.loadOp  = VriAttachmentLoadOp_Load;
                colorAttachment.storeOp = VriAttachmentStoreOp_Store;
                VriAttachmentDesc depthAttachment {};
                depthAttachment.view                          = *depthView;
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
                auto drawn = drawSceneExpected(cmd, GeometryPass::eForward);
                m_Device.core.CmdEndRendering(cmd);
                return drawn;
            });
    }

    std::expected<BuiltinRenderer::GBuffer, std::string>
    BuiltinRenderer::addGBufferPassesExpected(RenderGraph& graph, RenderGraph::Resource depth, Extent size)
    {
        if (&graph.device() != &m_Device || size.empty())
        {
            return std::unexpected("G-buffer requires its device and nonzero extent");
        }
        if (auto valid = rendererImageExpected(graph, depth, VriFormat_D32_SFLOAT, size); !valid)
        {
            return std::unexpected("G-buffer depth requires D32F at the view extent: " + valid.error());
        }
        RenderGraph::AppendTransaction transaction(graph);
        GBuffer                        gbuffer {};
        for (size_t i = 0; i < gbuffer.size(); ++i)
        {
            auto created = graph.createTextureExpected(kGBufferNames[i], colorTexture(size, kGBufferFormats[i]));
            if (!created)
            {
                return std::unexpected(created.error());
            }
            gbuffer[i] = *created;
        }
        for (uint32_t stage = 0; stage < 2; ++stage)
        {
            const uint32_t                first = stage == 0 ? 0 : 4;
            const uint32_t                count = stage == 0 ? 4 : 3;
            std::vector<RenderGraph::Use> uses;
            for (uint32_t i = 0; i < count; ++i)
            {
                uses.push_back({gbuffer[first + i], Usage::eColorWrite});
            }
            uses.push_back({depth, stage == 0 ? Usage::eDepthWrite : Usage::eDepthRead});
            auto added = graph.addPassExpected(
                stage == 0 ? "G-buffer geometry" : "G-buffer material",
                uses,
                [this, gbuffer, depth, size, stage, first, count](auto* cmd,
                                                                  auto& resources) -> std::expected<void, std::string>
                {
                    if (auto ready = requirePreparedExpected(resources); !ready)
                    {
                        return ready;
                    }
                    std::array<VriAttachmentDesc, 4> colors {};
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        auto view = graphViewExpected(resources, gbuffer[first + i]);
                        if (!view)
                        {
                            return std::unexpected(view.error());
                        }
                        colors[i].view    = *view;
                        colors[i].loadOp  = VriAttachmentLoadOp_Clear;
                        colors[i].storeOp = VriAttachmentStoreOp_Store;
                    }
                    if (stage == 0)
                    {
                        colors[0].clearValue.color.f32[3] = -1;
                    }
                    auto view = graphViewExpected(resources, depth);
                    if (!view)
                    {
                        return std::unexpected(view.error());
                    }
                    VriAttachmentDesc depthAttachment {};
                    depthAttachment.view    = *view;
                    depthAttachment.loadOp  = stage == 0 ? VriAttachmentLoadOp_Clear : VriAttachmentLoadOp_Load;
                    depthAttachment.storeOp = VriAttachmentStoreOp_Store;
                    depthAttachment.clearValue.depthStencil.depth = 1;
                    VriAttachmentsDesc attachments {};
                    attachments.colors     = colors.data();
                    attachments.colorNum   = count;
                    attachments.depth      = &depthAttachment;
                    attachments.renderArea = {0, 0, size.width, size.height};
                    attachments.layerNum   = 1;
                    m_Device.core.CmdBeginRendering(cmd, &attachments);
                    setViewport(m_Device, cmd, size);
                    auto drawn =
                        drawSceneExpected(cmd,
                                          stage == 0 ? GeometryPass::eGBufferBase : GeometryPass::eGBufferMaterial);
                    m_Device.core.CmdEndRendering(cmd);
                    return drawn;
                });
            if (!added)
            {
                return std::unexpected(added.error());
            }
        }
        transaction.commit();
        return gbuffer;
    }

    std::expected<void, std::string> BuiltinRenderer::addDeferredLightingPassExpected(RenderGraph&          graph,
                                                                                      RenderGraph::Resource hdr,
                                                                                      GBuffer               gbuffer,
                                                                                      ShadowMaps            shadows)
    {
        if (&graph.device() != &m_Device)
        {
            return std::unexpected("Built-in graph belongs to another device");
        }
        auto image = rendererImageExpected(graph, hdr, VriFormat_RGBA16_SFLOAT);
        if (!image)
        {
            return std::unexpected("Deferred lighting requires a linear RGBA16F target: " + image.error());
        }
        const Extent size {image->width, image->height};
        for (size_t i = 0; i < gbuffer.size(); ++i)
        {
            if (auto valid = rendererImageExpected(graph, gbuffer[i], kGBufferFormats[i], size); !valid)
            {
                return std::unexpected(std::string("Deferred lighting G-buffer contract mismatch: ") +
                                       kGBufferNames[i] + ": " + valid.error());
            }
        }
        auto shadowImage = rendererImageExpected(graph, shadows[0], VriFormat_D32_SFLOAT);
        if (!shadowImage)
        {
            return std::unexpected(shadowImage.error());
        }
        const Extent shadowSize {shadowImage->width, shadowImage->width};
        for (const auto shadow : shadows)
        {
            if (auto valid = rendererImageExpected(graph, shadow, VriFormat_D32_SFLOAT, shadowSize); !valid)
            {
                return std::unexpected("Deferred lighting requires four equally sized square D32F shadow maps: " +
                                       valid.error());
            }
        }
        std::vector<RenderGraph::Use> uses {{hdr, Usage::eColorReadWrite}};
        for (const auto resource : gbuffer)
        {
            uses.push_back({resource, Usage::eSampled});
        }
        for (const auto resource : shadows)
        {
            uses.push_back({resource, Usage::eSampled});
        }
        return graph.addPassExpected("Deferred OpenPBR",
                                     uses,
                                     [this, hdr, size](auto* cmd, auto& resources) -> std::expected<void, std::string>
                                     {
                                         if (auto ready = requirePreparedExpected(resources); !ready)
                                         {
                                             return ready;
                                         }
                                         auto view = graphViewExpected(resources, hdr);
                                         if (!view)
                                         {
                                             return std::unexpected(view.error());
                                         }
                                         beginColorPass(m_Device, cmd, *view, size);
                                         drawFullscreen(cmd, m_DeferredLighting->handle());
                                         m_Device.core.CmdEndRendering(cmd);
                                         return {};
                                     });
    }

    std::expected<RenderGraph::Resource, std::string>
    BuiltinRenderer::addToneMappingPassExpected(RenderGraph& graph, RenderGraph::Resource hdr, Extent size)
    {
        if (!graph.ownsResource(hdr) || size.empty())
        {
            return std::unexpected("Tone mapping needs a graph input and nonzero extent");
        }
        const auto info = graph.resourceInfo(hdr);
        if (info.textureDesc.width != size.width || info.textureDesc.height != size.height)
        {
            return std::unexpected("Tone mapping extent differs from its input");
        }
        const std::array inputs {hdr};
        auto added = m_ToneMapping->addPassesExpected(graph, "Tone mapping", inputs, m_PreparedFrame.toneParameters);
        if (!added)
        {
            return std::unexpected(added.error());
        }
        return added->front();
    }

    std::expected<BuiltinRenderer::Outputs, std::string> BuiltinRenderer::addScenePassesExpected(RenderGraph& graph,
                                                                                                 Extent       size)
    {
        if (&graph.device() != &m_Device || size.empty())
        {
            return std::unexpected("Built-in scene requires its device and nonzero extent");
        }
        if (settings.path == RenderPath::eReferencePathTracing)
        {
            return std::unexpected("Use ReferencePathTracer to build reference scene outputs");
        }
        if (settings.path != RenderPath::eNaiveDeferred && settings.path != RenderPath::eNaiveForward)
        {
            return std::unexpected("Unsupported built-in render path");
        }
        if (settings.meshShading && settings.path == RenderPath::eNaiveDeferred)
        {
            return std::unexpected("NaiveDeferred currently requires indexed geometry");
        }
        if (settings.meshShading && !m_Scene.meshlets)
        {
            return std::unexpected("Mesh shading requires uploaded meshlets");
        }
        RenderGraph::AppendTransaction transaction(graph);
        Outputs                        outputs;
        outputs.path = settings.path;
        auto shadows = addShadowPassesExpected(graph);
        if (!shadows)
        {
            return std::unexpected(shadows.error());
        }
        outputs.shadows = *shadows;
        auto hdr        = graph.createTextureExpected("scene_hdr", colorTexture(size, VriFormat_RGBA16_SFLOAT));
        if (!hdr)
        {
            return std::unexpected(hdr.error());
        }
        outputs.hdr    = *hdr;
        auto depthDesc = depthTexture(size);
        depthDesc.usage |= VriTextureUsage_TransferSrc;
        auto depth = graph.createTextureExpected("scene_depth", depthDesc);
        if (!depth)
        {
            return std::unexpected(depth.error());
        }
        outputs.depth = *depth;
        if (auto sky = addSkyboxPassExpected(graph, outputs.hdr); !sky)
        {
            return std::unexpected(sky.error());
        }
        if (outputs.path == RenderPath::eNaiveDeferred)
        {
            auto gbuffer = addGBufferPassesExpected(graph, outputs.depth, size);
            if (!gbuffer)
            {
                return std::unexpected(gbuffer.error());
            }
            outputs.gbuffer = *gbuffer;
            if (auto lighting = addDeferredLightingPassExpected(graph, outputs.hdr, outputs.gbuffer, outputs.shadows);
                !lighting)
            {
                return std::unexpected(lighting.error());
            }
        }
        else if (auto forward = addForwardPassExpected(graph, outputs.hdr, outputs.depth, outputs.shadows); !forward)
        {
            return std::unexpected(forward.error());
        }
        transaction.commit();
        return outputs;
    }

    std::expected<BuiltinRenderer::Outputs, std::string> BuiltinRenderer::addPassesExpected(RenderGraph& graph,
                                                                                            Extent       size)
    {
        RenderGraph::AppendTransaction transaction(graph);
        auto                           outputs = addScenePassesExpected(graph, size);
        if (!outputs)
        {
            return std::unexpected(outputs.error());
        }
        auto color = addToneMappingPassExpected(graph, outputs->hdr, size);
        if (!color)
        {
            return std::unexpected(color.error());
        }
        outputs->color = *color;
        transaction.commit();
        return outputs;
    }

    void BuiltinRenderer::prepare(const RenderCamera&                         camera,
                                  RenderGraph&                                graph,
                                  const Outputs&                              outputs,
                                  std::optional<std::span<const RenderLight>> lights,
                                  float                                       environmentIntensity)
    {
        if (auto prepared = prepareExpected(camera, graph, outputs, lights, environmentIntensity); !prepared)
        {
            throw std::invalid_argument(prepared.error()); // Legacy boundary.
        }
    }

    std::expected<void, std::string>
    BuiltinRenderer::prepareExpected(const RenderCamera&                         camera,
                                     RenderGraph&                                graph,
                                     const Outputs&                              outputs,
                                     std::optional<std::span<const RenderLight>> lights,
                                     float                                       environmentIntensity)
    {
        m_PreparedGraph = nullptr;
        if (&graph.device() != &m_Device || !graph.m_Compiled)
        {
            return std::unexpected("Compile the built-in graph on its device before preparation");
        }
        if (outputs.path != settings.path ||
            (outputs.path != RenderPath::eNaiveForward && outputs.path != RenderPath::eNaiveDeferred))
        {
            return std::unexpected("Rebuild the built-in graph after changing the render path");
        }
        if (!m_Scene.vertices || !m_Scene.indices || !m_Scene.transforms ||
            m_SceneBuffers !=
                std::array {m_Scene.vertices->handle, m_Scene.indices->handle, m_Scene.transforms->handle} ||
            m_Scene.materials.size() != m_ShaderMaterials.size() ||
            m_Scene.materialTextures.size() != m_ShaderMaterials.size())
        {
            return std::unexpected("Rebuild the built-in renderer after replacing scene geometry or material tables");
        }
        for (const auto& primitive : m_Scene.primitives)
        {
            if (primitive.material >= m_ShaderMaterials.size() ||
                primitive.firstIndex > m_Scene.indices->desc.size / sizeof(uint32_t) ||
                primitive.indexCount > m_Scene.indices->desc.size / sizeof(uint32_t) - primitive.firstIndex)
            {
                return std::unexpected("Built-in primitive indices/material are outside the uploaded scene");
            }
        }
        for (const auto value : {settings.exposure,
                                 settings.shadowBias,
                                 settings.normalBias,
                                 settings.sunAngularRadius,
                                 settings.cascadeBlend,
                                 settings.roughnessOverride,
                                 settings.metalnessOverride,
                                 settings.lightIntensity,
                                 settings.lightColor.x,
                                 settings.lightColor.y,
                                 settings.lightColor.z})
        {
            if (!std::isfinite(value))
            {
                return std::unexpected("Built-in numeric settings must be finite");
            }
        }
        if (uint32_t(settings.shadowFilter) > uint32_t(ShadowFilter::ePcss) ||
            uint32_t(settings.toneOperator) > uint32_t(ToneOperator::eReinhard))
        {
            return std::unexpected("Invalid built-in shadow filter or tone operator");
        }
        for (const auto* texture : {m_Environment.radiance.get(),
                                    m_Environment.diffuse.get(),
                                    m_Environment.specular.get(),
                                    m_Environment.brdfLut.get()})
        {
            if (!texture || !texture->belongsTo(m_Device) ||
                !(m_Device.core.GetFormatSupport(m_Device.handle, texture->desc.format) &
                  VriFormatSupport_LinearFilter))
            {
                return std::unexpected(
                    "Built-in environment requires complete, native-filterable images on its device");
            }
        }
        for (const auto& textures : m_Scene.materialTextures)
        {
            for (const auto* texture : textures)
            {
                if (!texture || !texture->belongsTo(m_Device))
                {
                    return std::unexpected("Invalid built-in material texture ownership");
                }
            }
        }
        auto&      pending                       = m_CandidateFrame;
        const auto effectiveEnvironmentIntensity = settings.environmentIntensity * environmentIntensity;
        if (!std::isfinite(settings.ambientColor.x) || !std::isfinite(settings.ambientColor.y) ||
            !std::isfinite(settings.ambientColor.z) || glm::any(glm::lessThan(settings.ambientColor, glm::vec3(0))))
        {
            return std::unexpected("Renderer ambient color must be nonnegative and finite");
        }
        if (!std::isfinite(environmentIntensity) || environmentIntensity < 0 ||
            !std::isfinite(effectiveEnvironmentIntensity) || effectiveEnvironmentIntensity < 0)
        {
            return std::unexpected("Renderer environment intensity must be nonnegative and finite");
        }
        if (settings.meshShading && !m_MeshForward[0])
        {
            return std::unexpected("Mesh shading requires a GpuScene created with meshlets");
        }
        FrameData data {};
        auto      directionToLight = settings.directionToLight;
        auto      lightColor       = settings.lightColor;
        auto      lightIntensity   = settings.lightIntensity;
        data.lightConfig.y         = -1;
        if (lights)
        {
            if (lights->size() > kMaxRenderLights)
            {
                return std::unexpected("Renderer exceeds its 64-light limit");
            }
            lightIntensity     = 0;
            data.lightConfig.x = float(lights->size());
            for (size_t i = 0; i < lights->size(); ++i)
            {
                const auto& light  = (*lights)[i];
                const auto  finite = [](glm::vec3 value)
                {
                    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
                };
                if (light.kind > RenderLightKind::eSpot || !finite(light.position) || !finite(light.directionToLight) ||
                    !finite(light.color) || glm::any(glm::lessThan(light.color, glm::vec3(0))) ||
                    !std::isfinite(light.intensity) || light.intensity < 0 || !std::isfinite(light.range) ||
                    light.range <= 0 || !std::isfinite(light.innerCone) || !std::isfinite(light.outerCone) ||
                    light.innerCone < 0 || light.innerCone >= light.outerCone ||
                    light.outerCone >= std::numbers::pi_v<float> * 0.5f)
                {
                    return std::unexpected("Invalid render light at index " + std::to_string(i));
                }
                auto direction = glm::vec3(0, 0, 1);
                if (light.kind != RenderLightKind::ePoint)
                {
                    const auto length = glm::length(light.directionToLight);
                    if (!std::isfinite(length) || length == 0)
                    {
                        return std::unexpected("Render light direction is singular at index " + std::to_string(i));
                    }
                    direction = light.directionToLight / length;
                }
                data.lights[i] = {{light.position, light.range},
                                  {direction, float(light.kind)},
                                  {light.color, light.intensity},
                                  {std::cos(light.innerCone), std::cos(light.outerCone), 0, 0}};
                if (light.kind == RenderLightKind::eDirectional && data.lightConfig.y < 0)
                {
                    data.lightConfig.y = float(i);
                    directionToLight   = direction;
                    lightColor         = light.color;
                    lightIntensity     = light.intensity;
                }
            }
        }
        for (const auto resource : outputs.shadows)
        {
            if (resource.graph && !graph.ownsResource(resource))
            {
                return std::unexpected("Foreign built-in shadow resource");
            }
        }
        if (!graph.ownsResource(outputs.hdr))
        {
            return std::unexpected("Built-in HDR output belongs to another graph");
        }
        const auto shadowResolution = outputs.shadows[0].graph ?
                                          graph.resourceInfo(outputs.shadows[0]).textureDesc.width :
                                          settings.shadowResolution;
        auto       cascadeResult    = calculateCascadesExpected(camera,
                                                       directionToLight,
                                                       m_Scene.center,
                                                       m_Scene.radius,
                                                       shadowResolution,
                                                       settings.splitLambda);
        if (!cascadeResult)
        {
            return std::unexpected(cascadeResult.error());
        }
        const auto& cascades        = *cascadeResult;
        data.viewProjection         = camera.projection * camera.view;
        data.inverseViewProjection  = glm::inverse(data.viewProjection);
        data.view                   = camera.view;
        data.shadowViewDepth        = shadowDepthPlane(camera.view);
        data.lightViewProjection    = cascades.viewProjection;
        data.cameraPosition         = glm::inverse(camera.view)[3];
        // Borrowed game shaders may depend on arbitrary data, so their shadow contents are not cached here.
        pending.cacheShadows = settings.cacheShadows && std::ranges::none_of(m_ShaderMaterials,
                                                                             [](const auto* material)
                                                                             {
                                                                                 return material != nullptr;
                                                                             });
        pending.shadowMaterials.clear();
        if (pending.cacheShadows)
        {
            pending.shadowMaterials.resize(m_Scene.materials.size());
            for (size_t i = 0; i < pending.shadowMaterials.size(); ++i)
            {
                const auto& material = m_Scene.materials[i];
                auto        view     = m_Scene.materialTextures[i][0]->viewExpected();
                if (!view)
                {
                    return std::unexpected(view.error());
                }
                pending.shadowMaterials[i] = {material.baseColor.a,
                                              material.alphaCutoff,
                                              material.baseColorTexture.sampler,
                                              material.doubleSided,
                                              *view};
            }
        }
        const bool invalidate = !pending.cacheShadows || pending.shadowMaterials != m_PreparedFrame.shadowMaterials;
        std::array<uint64_t, 4> shadowPrograms;
        for (size_t i = 0; i < shadowPrograms.size(); ++i)
        {
            shadowPrograms[i]       = m_Shadow[i]->generation();
            pending.shadowKeys[i]   = {outputs.shadows[i].graph,
                                       outputs.shadows[i].index,
                                       data.lightViewProjection[i],
                                       m_Scene.transformRevision(),
                                       m_Scene.geometryRevision(),
                                       {},
                                       settings.shadowFilter != ShadowFilter::eDisabled};
        }
        for (auto& key : pending.shadowKeys)
        {
            key.programs = shadowPrograms;
        }
        for (size_t volume = 0; volume < pending.visiblePrimitives.size(); ++volume)
        {
            auto& visible = pending.visiblePrimitives[volume];
            visible.clear();
            if (volume &&
                (!outputs.shadows[volume - 1].graph || !graph.resourceInfo(outputs.shadows[volume - 1]).active))
            {
                continue;
            }
            if (volume && pending.cacheShadows && !invalidate && m_ShadowCache[volume - 1] &&
                *m_ShadowCache[volume - 1] == pending.shadowKeys[volume - 1])
            {
                continue;
            }
            const auto planes = frustumPlanes(volume ? data.lightViewProjection[volume - 1] : data.viewProjection);
            visible.reserve(m_Scene.primitives.size());
            for (uint32_t index = 0; index < m_Scene.primitives.size(); ++index)
            {
                auto bounds = m_Scene.primitiveBoundsExpected(index);
                if (!bounds)
                {
                    return std::unexpected(bounds.error());
                }
                // Explicit game vertex programs may displace vertices beyond the imported bounds.
                if (m_Scene.primitives[index].indexCount &&
                    (m_ShaderMaterials[m_Scene.primitives[index].material] || intersectsFrustum(**bounds, planes)))
                {
                    visible.push_back(index);
                }
            }
        }
        data.lightDirection     = {glm::normalize(directionToLight), lightIntensity};
        data.lightColor         = {lightColor, effectiveEnvironmentIntensity};
        data.ambientColor       = {settings.ambientColor, 0};
        data.cascadeSplits      = cascades.splits;
        data.cascadeWidths      = cascades.worldWidths;
        data.cascadeDepthRanges = cascades.depthRanges;
        data.shadowParameters   = {settings.shadowBias,
                                   settings.normalBias,
                                   settings.sunAngularRadius,
                                   float(settings.shadowFilter)};
        data.options            = {settings.ibl ? 1.0f : 0.0f,
                                   settings.skybox ? 1.0f : 0.0f,
                                   settings.exposure,
                                   float(m_Environment.specular->desc.mipNum - 1)};
        data.cameraClip         = {camera.nearPlane,
                                   camera.farPlane,
                                   std::clamp(settings.cascadeBlend, 0.0f, 0.15f),
                                   float(settings.debugMode)};
        pending.toneParameters  = {settings.exposure,
                                  settings.meshShading && settings.meshletColors ? 1.0 : 0.0,
                                   double(settings.toneOperator)};
        data.overrides          = {settings.roughnessOverride,
                                   settings.metalnessOverride,
                                   m_OutputFormat == VriFormat_RGBA16_SFLOAT ? 0.0f : 1.0f,
                                   settings.meshShading && settings.meshletColors ? 1.0f : 0.0f};
        auto descriptors        = frameViewsExpected(graph, outputs);
        if (!descriptors)
        {
            return std::unexpected(descriptors.error());
        }
        for (uint32_t slot = 0; slot < m_ShaderMaterials.size(); ++slot)
        {
            m_CandidatePipelines[slot] = {};
            if (auto* material = m_ShaderMaterials[slot])
            {
                auto pipelines = prepareShaderMaterialExpected(*material, graph, outputs);
                if (!pipelines)
                {
                    return std::unexpected(pipelines.error());
                }
                m_CandidatePipelines[slot] = *pipelines;
            }
        }
        auto* mapped = m_Device.core.MapBuffer(m_FrameBuffer->handle, 0, sizeof(data));
        if (!mapped)
        {
            return std::unexpected("Map renderer frame data");
        }
        std::memcpy(mapped, &data, sizeof(data));
        m_Device.core.UnmapBuffer(m_FrameBuffer->handle);
        std::array<VriDescriptorRangeUpdateDesc, 20> updates {};
        for (size_t i = 0; i < updates.size(); ++i)
        {
            updates[i] = {&(*descriptors)[i], 1};
        }
        m_Device.core.UpdateDescriptorRanges(m_FrameSet, 0, uint32_t(updates.size()), updates.data());
        if (invalidate)
        {
            invalidateShadowCache();
        }
        m_RecordedShadows = {};
        std::swap(m_PreparedFrame, m_CandidateFrame);
        m_ShaderPipelines.swap(m_CandidatePipelines);
        m_PreparedGraph = &graph;
        return {};
    }

    std::expected<void, std::string> BuiltinRenderer::requirePreparedExpected(const RenderGraph& graph) const
    {
        if (m_PreparedGraph != &graph)
        {
            return std::unexpected("Prepare the built-in renderer successfully for this graph before recording");
        }
        return {};
    }

    std::expected<std::array<const VriDescriptor*, 20>, std::string>
    BuiltinRenderer::frameViewsExpected(RenderGraph& graph, const Outputs& outputs)
    {
        if (&graph.device() != &m_Device || !graph.m_Compiled)
        {
            return std::unexpected("Compile the built-in graph on its device before resolving frame resources");
        }
        if (!m_Environment.radiance || !m_Environment.radiance->belongsTo(m_Device))
        {
            return std::unexpected("Built-in radiance is missing or belongs to another device");
        }
        auto fallback = m_Environment.radiance->viewExpected();
        if (!fallback)
        {
            return std::unexpected(fallback.error());
        }
        const auto resourceView = [&](RenderGraph::Resource resource) -> std::expected<VriDescriptor*, std::string>
        {
            if (!resource.graph)
            {
                return *fallback;
            }
            if (!graph.ownsResource(resource))
            {
                return std::unexpected("Foreign built-in frame resource");
            }
            const auto info = graph.resourceInfo(resource);
            if (!info.isTexture)
            {
                return std::unexpected("Built-in frame resource must be an image");
            }
            if (!info.active)
            {
                return *fallback;
            }
            return graphViewExpected(graph, resource);
        };
        std::array<const VriDescriptor*, 20> descriptors {};
        descriptors[0]  = m_FrameView;
        descriptors[10] = m_EnvironmentSampler;
        descriptors[19] = m_TransformView;
        for (size_t i = 0; i < outputs.shadows.size(); ++i)
        {
            auto view = resourceView(outputs.shadows[i]);
            if (!view)
            {
                return std::unexpected(view.error());
            }
            descriptors[i + 1] = *view;
        }
        const std::array     textures {m_Environment.radiance.get(),
                                   m_Environment.diffuse.get(),
                                   m_Environment.specular.get(),
                                   m_Environment.brdfLut.get(),
                                   m_OpenPbrLuts.get()};
        constexpr std::array slots {5u, 6u, 7u, 8u, 11u};
        for (size_t i = 0; i < textures.size(); ++i)
        {
            if (!textures[i] || !textures[i]->belongsTo(m_Device))
            {
                return std::unexpected("Built-in lighting resource is missing or belongs to another device");
            }
            auto view = textures[i]->viewExpected();
            if (!view)
            {
                return std::unexpected(view.error());
            }
            descriptors[slots[i]] = *view;
        }
        auto hdr = resourceView(outputs.hdr);
        if (!hdr)
        {
            return std::unexpected(hdr.error());
        }
        descriptors[9] = *hdr;
        for (size_t i = 0; i < outputs.gbuffer.size(); ++i)
        {
            auto view = resourceView(outputs.path == RenderPath::eNaiveDeferred ? outputs.gbuffer[i] : outputs.hdr);
            if (!view)
            {
                return std::unexpected(view.error());
            }
            descriptors[i + 12] = *view;
        }
        return descriptors;
    }

    std::array<VriPipeline*, 10>
    BuiltinRenderer::prepareShaderMaterial(ShaderMaterial& material, RenderGraph& graph, const Outputs& outputs)
    {
        auto prepared = prepareShaderMaterialExpected(material, graph, outputs);
        if (!prepared)
        {
            throw std::invalid_argument(prepared.error()); // Legacy boundary.
        }
        return *prepared;
    }

    std::expected<std::array<VriPipeline*, 10>, std::string>
    BuiltinRenderer::prepareShaderMaterialExpected(ShaderMaterial& material, RenderGraph& graph, const Outputs& outputs)
    {
        if (!material.belongsTo(m_Device))
        {
            return std::unexpected("Shader material belongs to another device");
        }
        if (std::ranges::find(m_ShaderMaterials, &material) != m_ShaderMaterials.end())
        {
            m_PreparedGraph = nullptr;
        }
        auto frameViews = frameViewsExpected(graph, outputs);
        if (!frameViews)
        {
            return std::unexpected(frameViews.error());
        }
        const auto&                                views = *frameViews;
        std::vector<ShaderResourceViews>           shaderResources;
        constexpr std::array                       frameNames {"frame",
                                                               "shadow0",
                                                               "shadow1",
                                                               "shadow2",
                                                               "shadow3",
                                                               "radianceMap",
                                                               "diffuseMap",
                                                               "specularMap",
                                                               "brdfLut",
                                                               "hdrColor",
                                                               "environmentSampler",
                                                               "openPbrLuts"};
        for (uint32_t i = 0; i < frameNames.size(); ++i)
        {
            auto type = VriDescriptorType_Texture;
            if (i == 0)
            {
                type = VriDescriptorType_ConstantBuffer;
            }
            else if (i == 10)
            {
                type = VriDescriptorType_Sampler;
            }
            shaderResources.push_back({frameNames[i], type, {views[i]}});
        }
        shaderResources.push_back({"primitiveTransforms", VriDescriptorType_StructuredBuffer, {m_TransformView}});
        if (m_Scene.meshlets)
        {
            constexpr std::array names {"sceneVertices", "meshlets", "meshletVertices", "meshletTriangles"};
            for (uint32_t i = 0; i < names.size(); ++i)
            {
                shaderResources.push_back({names[i], VriDescriptorType_StructuredBuffer, {m_Scene.meshlets->views[i]}});
            }
        }
        constexpr std::array         modes {"ShadowCaster", "Forward", "GBufferBase", "GBufferMaterial", "Forward"};
        std::array<VriPipeline*, 10> pipelines {};
        for (uint32_t purpose = 0; purpose < modes.size(); ++purpose)
        {
            if ((purpose == 4 && !settings.meshShading) ||
                ((purpose == 2 || purpose == 3) && outputs.path != RenderPath::eNaiveDeferred))
            {
                continue;
            }
            ShaderPassContext context;
            context.depth      = VriFormat_D32_SFLOAT;
            context.streams    = {{sizeof(SceneVertex), 0, VriVertexStepRate_PerVertex}};
            context.attributes = {{VriFormat_RGB32_SFLOAT, uint32_t(offsetof(SceneVertex, position)), 0},
                                  {VriFormat_RGB32_SFLOAT, uint32_t(offsetof(SceneVertex, normal)), 0},
                                  {VriFormat_RG32_SFLOAT, uint32_t(offsetof(SceneVertex, uv)), 0},
                                  {VriFormat_RGBA32_SFLOAT, uint32_t(offsetof(SceneVertex, color)), 0},
                                  {VriFormat_RGBA32_SFLOAT, uint32_t(offsetof(SceneVertex, tangent)), 0}};
            if (purpose == 1 || purpose == 4)
            {
                context.colors = {VriFormat_RGBA16_SFLOAT};
            }
            else if (purpose == 2)
            {
                context.colors.assign(kGBufferFormats.begin(), kGBufferFormats.begin() + 4);
            }
            else if (purpose == 3)
            {
                context.colors.assign(kGBufferFormats.begin() + 4, kGBufferFormats.end());
                context.depthReadOnly = true;
            }
            auto pass = material.passForModeExpected(modes[purpose], purpose == 4);
            if (!pass)
            {
                return std::unexpected(pass.error());
            }
            for (uint32_t mirrored = 0; mirrored < 2; ++mirrored)
            {
                context.mirrored = mirrored != 0;
                auto pipeline    = material.prepareExpected((*pass)->name, context, shaderResources);
                if (!pipeline)
                {
                    return std::unexpected(pipeline.error());
                }
                auto pushSize = material.pushConstantSizeExpected((*pass)->name);
                if (!pushSize)
                {
                    return std::unexpected(pushSize.error());
                }
                if (*pushSize != 32)
                {
                    return std::unexpected("Built-in shader contract requires the 32-byte vultraDraw block: " +
                                           (*pass)->name);
                }
                pipelines[mirrored * 5 + purpose] = *pipeline;
            }
        }
        return pipelines;
    }

    std::string BuiltinRenderer::shaderSubshaderCompatibility(const ShaderSubshader& subshader)
    {
        auto compatible = shaderSubshaderCompatibilityExpected(subshader);
        if (!compatible)
        {
            throw std::invalid_argument(compatible.error()); // Legacy boundary.
        }
        return *compatible;
    }

    std::expected<std::string, std::string>
    BuiltinRenderer::shaderSubshaderCompatibilityExpected(const ShaderSubshader& subshader)
    {
        for (const auto& pass : subshader.passes)
        {
            uint32_t colors = 0;
            if (pass.lightMode == "Forward" || (pass.generated && pass.name == "MeshForward"))
            {
                colors = 1;
            }
            else if (pass.lightMode == "GBufferBase")
            {
                colors = 4;
            }
            else if (pass.lightMode == "GBufferMaterial")
            {
                colors = 3;
            }
            else if (pass.lightMode != "ShadowCaster" && pass.lightMode != "DepthOnly")
            {
                continue;
            }
            for (const auto& [variant, program] : pass.programs)
            {
                uint32_t outputs = 0;
                for (const auto& shader : program.shaders)
                {
                    outputs = std::max(outputs, shader.colorOutputs);
                }
                if (outputs != colors)
                {
                    return pass.name + " / " + variant + ": color attachment count differs from the raster contract";
                }
                auto bindings = program.resourceBindingsExpected();
                if (!bindings)
                {
                    return std::unexpected(pass.name + " / " + variant + ": " + bindings.error());
                }
                const auto draw = std::ranges::find(*bindings, "vultraDraw", &ShaderResourceBinding::name);
                if (draw == bindings->end() || !draw->pushConstant || draw->uniformSize != 32)
                {
                    return pass.name + " / " + variant + ": requires the 32-byte vultraDraw push-constant contract";
                }
            }
        }
        return std::string {};
    }

    std::array<uint32_t, 5> BuiltinRenderer::primitiveCounts() const
    {
        std::array<uint32_t, 5> result;
        for (size_t i = 0; i < result.size(); ++i)
        {
            result[i] = i == 0 || m_ShadowDrawn[i - 1] ? uint32_t(m_PreparedFrame.visiblePrimitives[i].size()) : 0;
        }
        return result;
    }

    void BuiltinRenderer::completeFrame()
    {
        for (size_t i = 0; i < m_ShadowCache.size(); ++i)
        {
            if (m_RecordedShadows[i])
            {
                m_ShadowCache[i] = std::move(m_RecordedShadows[i]);
                m_RecordedShadows[i].reset();
            }
        }
    }

    void BuiltinRenderer::invalidateShadowCache()
    {
        m_ShadowCache     = {};
        m_RecordedShadows = {};
    }

    std::expected<void, std::string> BuiltinRenderer::prepareToneMappingExpected(RenderGraph&          graph,
                                                                                 RenderGraph::Resource hdr)
    {
        if (&graph.device() != &m_Device || !graph.m_Compiled || !std::isfinite(settings.exposure) ||
            uint32_t(settings.toneOperator) > uint32_t(ToneOperator::eReinhard))
        {
            return std::unexpected("Tone mapping preparation requires its compiled graph and finite valid settings");
        }
        auto texture = graph.getTextureExpected(hdr);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        m_PreparedFrame.toneParameters = {settings.exposure, 0, double(settings.toneOperator)};
        return {};
    }

    void BuiltinRenderer::prepareToneMapping(RenderGraph& graph, RenderGraph::Resource hdr)
    {
        if (auto prepared = prepareToneMappingExpected(graph, hdr); !prepared)
        {
            throw std::invalid_argument(prepared.error()); // Legacy boundary.
        }
    }

    std::expected<void, std::string> BuiltinRenderer::setShaderMaterialExpected(uint32_t slot, ShaderMaterial* material)
    {
        if (slot >= m_ShaderMaterials.size())
        {
            return std::unexpected("Shader material slot is out of range");
        }
        if (material && !material->belongsTo(m_Device))
        {
            return std::unexpected("Shader material belongs to another device");
        }
        m_ShaderMaterials[slot] = material;
        m_ShaderPipelines[slot] = {};
        m_PreparedGraph         = nullptr;
        return {};
    }

    void BuiltinRenderer::setShaderMaterial(uint32_t slot, ShaderMaterial* material)
    {
        if (auto assigned = setShaderMaterialExpected(slot, material); !assigned)
        {
            throw std::out_of_range(assigned.error()); // Legacy boundary.
        }
    }

    std::expected<bool, std::string> BuiltinRenderer::pollShadersExpected()
    {
        m_PreparedGraph     = nullptr;
        bool        changed = false;
        std::string errors;
        const auto  poll = [&](ShaderPipeline* pipeline)
        {
            if (!pipeline)
            {
                return;
            }
            auto result = pipeline->pollExpected();
            if (!result)
            {
                if (!errors.empty())
                {
                    errors += "\n";
                }
                errors += result.error();
            }
            else
            {
                changed |= *result;
            }
        };
        for (uint32_t variant = 0; variant < 4; ++variant)
        {
            poll(m_Shadow[variant].get());
            poll(m_Forward[variant].get());
            poll(m_ForwardOpaque[variant].get());
            poll(m_GBufferBase[variant].get());
            poll(m_GBufferMaterial[variant].get());
            poll(m_MeshForward[variant].get());
        }
        poll(m_Skybox.get());
        poll(&m_ToneMapping->shader());
        poll(m_DeferredLighting.get());
        if (!errors.empty())
        {
            return std::unexpected(errors);
        }
        return changed;
    }

    void BuiltinRenderer::pollShaders()
    {
        if (auto polled = pollShadersExpected(); !polled)
        {
            Logger::core().error("[Built-in shader] {}", polled.error()); // Legacy diagnostics boundary.
        }
    }

    std::string BuiltinRenderer::diagnostics() const
    {
        auto result =
            m_Skybox->diagnostics() + m_ToneMapping->shader().diagnostics() + m_DeferredLighting->diagnostics();
        for (uint32_t variant = 0; variant < 4; ++variant)
        {
            result += m_Shadow[variant]->diagnostics() + m_Forward[variant]->diagnostics() +
                      m_GBufferBase[variant]->diagnostics() + m_GBufferMaterial[variant]->diagnostics();
            if (m_ForwardOpaque[variant])
            {
                result += m_ForwardOpaque[variant]->diagnostics();
            }
            if (m_MeshForward[variant])
            {
                result += m_MeshForward[variant]->diagnostics();
            }
        }
        return result;
    }
} // namespace vultra
