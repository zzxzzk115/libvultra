#include <vultra/render/environment.hpp>
#include <vultra/render/forward_path.hpp>
#include <vultra/render/openpbr_luts.hpp>
#include <vultra/render/scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

namespace vultra
{
    namespace
    {
        // Keep these layouts in sync with builtin/shaders/resources/frame_block.slangh. The shared block also
        // carries shadow-cascade fields for paths that render shadows; this path leaves them disabled.
        struct FrameData
        {
            glm::mat4                viewProjection;
            glm::mat4                inverseViewProjection;
            glm::mat4                view;
            std::array<glm::mat4, 4> lightViewProjection;
            glm::vec4                cameraPosition;
            glm::vec4                lightDirection; // xyz to the main light, w intensity.
            glm::vec4                lightColor;     // rgb main light, w environment intensity.
            glm::vec4                ambientColor;
            glm::vec4                cascadeSplits;
            glm::vec4                cascadeWidths;
            glm::vec4                cascadeDepthRanges;
            glm::vec4                shadowParameters; // w = 0 disables shadow sampling.
            glm::vec4                options;          // IBL, skybox, unused, specular mip count - 1.
            glm::vec4                cameraClip;       // near, far, unused, debug mode.
            glm::vec4                overrides;        // Negative roughness/metalness keep materials.
            glm::vec4                lightConfig;      // Count, main directional index (-1 if absent).
        };

        struct MaterialData
        {
            glm::vec4  baseColor;
            glm::vec4  surface;
            glm::vec4  emission;
            glm::vec4  coat;
            glm::vec4  specular;
            glm::vec4  flags;
            glm::uvec4 meshlets;
            glm::uvec4 instance; // Primitive transform index.
        };

        static_assert(offsetof(FrameData, lightConfig) == 624 && sizeof(FrameData) == 640);
        static_assert(sizeof(MaterialData) == 128);
        static_assert(sizeof(SceneVertex) == 64 && offsetof(SceneVertex, tangent) == 48);

        constexpr uint32_t kFrameBindings = 21;

        std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& matrix)
        {
            const glm::vec4 x {matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]};
            const glm::vec4 y {matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]};
            const glm::vec4 z {matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2]};
            const glm::vec4 w {matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]};
            std::array      planes {w + x, w - x, w + y, w - y, z, w - z}; // VRI depth is [0,1].
            for (auto& plane : planes)
            {
                const float length = glm::length(glm::vec3(plane));
                if (length > 0)
                {
                    plane /= length;
                }
            }
            return planes;
        }

        bool intersects(const PrimitiveBounds& bounds, const std::array<glm::vec4, 6>& planes)
        {
            for (const auto& plane : planes)
            {
                const glm::vec3 normal(plane);
                const auto      support =
                    glm::dot(normal, bounds.center) + plane.w + glm::dot(glm::abs(normal), bounds.extent);
                // Keep touching boxes despite float transform/clip-plane roundoff.
                const auto margin = 1e-4f * (1 + glm::dot(glm::abs(normal), glm::abs(bounds.center) + bounds.extent));
                if (support < -margin)
                {
                    return false;
                }
            }
            return true;
        }

        std::expected<VriPipeline*, Error> createPipeline(Device&                        device,
                                                          VriPipelineLayout*             layout,
                                                          std::span<const VriShaderDesc> shaders,
                                                          bool                           mesh,
                                                          bool                           doubleSided,
                                                          bool                           mirrored)
        {
            VriVertexStreamDesc    stream {sizeof(SceneVertex), 0, VriVertexStepRate_PerVertex};
            VriVertexAttributeDesc attributes[5] {};
            attributes[0] = {.format = VriFormat_RGB32_SFLOAT, .offset = offsetof(SceneVertex, position)};
            attributes[1] = {.format = VriFormat_RGB32_SFLOAT, .offset = offsetof(SceneVertex, normal)};
            attributes[2] = {.format = VriFormat_RG32_SFLOAT, .offset = offsetof(SceneVertex, uv)};
            attributes[3] = {.format = VriFormat_RGBA32_SFLOAT, .offset = offsetof(SceneVertex, color)};
            attributes[4] = {.format = VriFormat_RGBA32_SFLOAT, .offset = offsetof(SceneVertex, tangent)};
            VriColorAttachmentDesc color {};
            color.format         = VriFormat_RGBA16_SFLOAT;
            color.colorWriteMask = VriColorWrite_RGBA;
            VriGraphicsPipelineDesc desc {};
            desc.pipelineCache  = device.pipelineCache;
            desc.pipelineLayout = layout;
            desc.shaders        = shaders.data();
            desc.shaderNum      = uint32_t(shaders.size());
            if (mesh)
            {
                desc.vertexInput                     = {attributes, 5, &stream, 1};
                desc.depthStencil.depthTest          = VRI_TRUE;
                desc.depthStencil.depthWrite         = VRI_TRUE;
                desc.depthStencil.depthCompareOp     = VriCompareOp_Less;
                desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
            }
            desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
            desc.rasterization.cullMode  = doubleSided ? VriCullMode_None : VriCullMode_Back;
            desc.rasterization.frontFace = mirrored ? VriFrontFace_Clockwise : VriFrontFace_CounterClockwise;
            desc.rasterization.lineWidth = 1;
            desc.multisample.sampleNum   = 1;
            desc.outputMerger.colors     = &color;
            desc.outputMerger.colorNum   = 1;
            VriPipeline* result          = nullptr;
            if (auto created = check(device.core.CreateGraphicsPipeline(device.handle, &desc, &result),
                                     "Create forward path pipeline");
                !created)
            {
                return std::unexpected(created.error());
            }
            return result;
        }

        VriSamplerDesc repeatSampler()
        {
            VriSamplerDesc sampler {};
            sampler.minFilter    = VriFilter_Linear;
            sampler.magFilter    = VriFilter_Linear;
            sampler.mipmapMode   = VriMipmapMode_Linear;
            sampler.addressModeU = VriAddressMode_Repeat;
            sampler.addressModeV = VriAddressMode_Repeat;
            sampler.addressModeW = VriAddressMode_Repeat;
            sampler.maxLod       = 32;
            return sampler;
        }

        class MeshStage final : public RenderStage
        {
        public:
            std::expected<SceneResources, Error> addPasses(StageBuild& build) override
            {
                SceneResources                                            resources;
                const std::array<std::pair<std::string_view, Buffer*>, 3> buffers {
                    std::pair {kSceneVertices, build.scene.vertices.get()},
                    std::pair {kSceneIndices, build.scene.indices.get()},
                    std::pair {kSceneInstances, build.scene.transforms.get()}};
                for (const auto& [name, buffer] : buffers)
                {
                    const auto imported = build.graph.importResource(build.graphName(name), *buffer);
                    if (!imported)
                    {
                        return std::unexpected(imported.error());
                    }
                    resources.push_back({std::string(name), *imported});
                }
                return resources;
            }

            std::expected<void, Error> prepare(RenderGraph& graph, const StageFrame& frame) override
            {
                static_cast<void>(graph);
                static_cast<void>(frame);
                return {};
            }
        };

        SceneResourceDesc bufferResource(std::string_view name, uint32_t elementSize)
        {
            return {std::string(name), VriFormat_Unknown, {}, true, elementSize};
        }
    } // namespace

    StageType meshStage()
    {
        return {{"vultra.mesh",
                 {"geometry"},
                 StageScope::eFrame,
                 {},
                 {bufferResource(kSceneVertices, sizeof(SceneVertex)),
                  bufferResource(kSceneIndices, sizeof(uint32_t)),
                  bufferResource(kSceneInstances, sizeof(PrimitiveTransform))}},
                [](Device&, const AssetSource*) -> std::expected<std::unique_ptr<RenderStage>, Error>
                {
                    return std::make_unique<MeshStage>();
                }};
    }

    StageType forwardShadingStage()
    {
        return {{std::string(ForwardShadingStage::kName),
                 {"shading"},
                 StageScope::eView,
                 {{std::string(kSceneVertices)},
                  {std::string(kSceneIndices)},
                  {std::string(kSceneInstances)},
                  {std::string(kSceneLights)}},
                 {{std::string(kSceneHdr), VriFormat_RGBA16_SFLOAT, "color.linear.hdr"},
                  {std::string(kSceneDepth), VriFormat_D32_SFLOAT, "depth"}}},
                [](Device& device, const AssetSource* shaders) -> std::expected<std::unique_ptr<RenderStage>, Error>
                {
                    auto stage = ForwardShadingStage::create(device, shaders);
                    if (!stage)
                    {
                        return std::unexpected(stage.error());
                    }
                    return std::move(*stage);
                }};
    }

    RenderPathDesc forwardPathDesc()
    {
        return {std::string(kForwardPath),
                {{"geometry", "vultra.mesh"}, {"shading", std::string(ForwardShadingStage::kName)}}};
    }

    std::expected<std::unique_ptr<RenderPath>, Error> createForwardPath(Device& device, const AssetSource* shaders)
    {
        const std::array stages {meshStage(), forwardShadingStage()};
        return RenderPath::create(device, forwardPathDesc(), stages, shaders);
    }

    ForwardShadingStage::ForwardShadingStage(Device& device) :
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<ForwardShadingStage>, Error> ForwardShadingStage::create(Device&            device,
                                                                                           const AssetSource* shaders)
    {
        // The destructor releases whatever was created when a later step fails.
        std::unique_ptr<ForwardShadingStage> stage(new ForwardShadingStage(device));
        auto                                 luts = createOpenPbrLuts(device);
        if (!luts)
        {
            return std::unexpected(luts.error());
        }
        stage->m_OpenPbrLuts            = std::move(*luts);
        auto environmentSampler         = repeatSampler();
        environmentSampler.addressModeV = VriAddressMode_ClampToEdge;
        environmentSampler.addressModeW = VriAddressMode_ClampToEdge;
        if (auto created =
                check(device.core.CreateSampler(device.handle, &environmentSampler, &stage->m_EnvironmentSampler),
                      "Create environment sampler");
            !created)
        {
            return std::unexpected(created.error());
        }

        const VriShaderStageFlags stages = VriShaderStage_Vertex | VriShaderStage_Fragment;
        VriDescriptorRangeDesc    frameRanges[kFrameBindings] {};
        for (uint32_t i = 0; i < kFrameBindings; ++i)
        {
            frameRanges[i] = {i, 1, VriDescriptorType_Texture, stages};
        }
        frameRanges[0].descriptorType  = VriDescriptorType_ConstantBuffer;
        frameRanges[10].descriptorType = VriDescriptorType_Sampler;
        frameRanges[19]                = {19, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Vertex};
        frameRanges[20]                = {20, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Fragment};
        VriDescriptorRangeDesc materialRanges[kMaterialTextureCount + 1] {};
        for (uint32_t i = 0; i <= kMaterialTextureCount; ++i)
        {
            materialRanges[i] = {i, 1, VriDescriptorType_Texture, VriShaderStage_Fragment};
        }
        materialRanges[kMaterialTextureCount].descriptorType = VriDescriptorType_Sampler;
        materialRanges[kMaterialTextureCount].descriptorNum  = kMaterialTextureCount;
        VriDescriptorSetDesc sets[2] {{0, frameRanges, kFrameBindings}, {1, materialRanges, kMaterialTextureCount + 1}};
        VriPushConstantDesc  push {0, sizeof(MaterialData), stages};
        VriPipelineLayoutDesc layout {};
        layout.descriptorSets   = sets;
        layout.descriptorSetNum = 2;
        layout.pushConstants    = &push;
        layout.pushConstantNum  = 1;
        layout.shaderStages     = stages;
        if (auto created = check(device.core.CreatePipelineLayout(device.handle, &layout, &stage->m_Layout),
                                 "Create forward shading layout");
            !created)
        {
            return std::unexpected(created.error());
        }

        auto*      self     = stage.get();
        const auto pipeline = [&](const char* file, bool mesh, bool doubleSided, bool mirrored)
        {
            return ShaderPipeline::create(
                device,
                std::filesystem::path("builtin/shaders/passes") / file,
                std::vector<ShaderEntry> {{mesh ? "vertexMain" : "screenVertex", VriShaderStage_Vertex},
                                          {"fragmentMain", VriShaderStage_Fragment}},
                [self, mesh, doubleSided, mirrored](std::span<const VriShaderDesc> stageShaders)
                {
                    return createPipeline(self->m_Device, self->m_Layout, stageShaders, mesh, doubleSided, mirrored);
                },
                "builtin/shaders",
                std::vector<std::filesystem::path> {"builtin/shaders", "external"},
                shaders);
        };
        for (uint32_t variant = 0; variant < stage->m_Forward.size(); ++variant)
        {
            auto forward = pipeline("forward.slang", true, (variant & 1) != 0, (variant & 2) != 0);
            if (!forward)
            {
                return std::unexpected(forward.error());
            }
            stage->m_Forward[variant] = std::move(*forward);
        }
        auto skybox = pipeline("skybox.slang", false, true, false);
        if (!skybox)
        {
            return std::unexpected(skybox.error());
        }
        stage->m_Skybox = std::move(*skybox);
        return stage;
    }

    ForwardShadingStage::~ForwardShadingStage()
    {
        m_Device.waitIdleForDestruction();
        releaseScene();
        for (auto& view : m_Views)
        {
            if (view.frameView)
            {
                m_Device.core.DestroyDescriptor(view.frameView);
            }
        }
        m_Views.clear();
        for (auto& pipeline : m_Forward)
        {
            pipeline.reset();
        }
        m_Skybox.reset();
        if (m_EnvironmentSampler)
        {
            m_Device.core.DestroyDescriptor(m_EnvironmentSampler);
            m_EnvironmentSampler = nullptr;
        }
        if (m_Layout)
        {
            m_Device.core.DestroyPipelineLayout(m_Layout);
            m_Layout = nullptr;
        }
    }

    void ForwardShadingStage::releaseScene()
    {
        if (m_Pool)
        {
            m_Device.core.DestroyDescriptorPool(m_Pool);
            m_Pool = nullptr;
        }
        for (auto* sampler : m_MaterialSamplers)
        {
            if (sampler)
            {
                m_Device.core.DestroyDescriptor(sampler);
            }
        }
        m_MaterialSamplers.clear();
        m_MaterialSets.clear();
        if (m_TransformView)
        {
            m_Device.core.DestroyDescriptor(m_TransformView);
            m_TransformView = nullptr;
        }
        for (auto& view : m_Views)
        {
            view.frameSet = nullptr;
            view.visible.clear();
        }
        m_Scene       = nullptr;
        m_Environment = nullptr;
    }

    std::expected<void, Error> ForwardShadingStage::bindScene(StageBuild& build, Buffer& instances)
    {
        releaseScene();
        auto&      scene     = build.scene;
        const auto viewCount = uint32_t(build.views.size());
        while (m_Views.size() < viewCount)
        {
            auto frameBuffer = Buffer::create(
                m_Device,
                VriBufferDesc {sizeof(FrameData), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
            if (!frameBuffer)
            {
                return std::unexpected(frameBuffer.error());
            }
            View view;
            view.frameBuffer = std::make_unique<Buffer>(std::move(*frameBuffer));
            const VriBufferViewDesc frameView {view.frameBuffer->handle,
                                               VriDescriptorType_ConstantBuffer,
                                               VriFormat_Unknown,
                                               0,
                                               sizeof(FrameData)};
            if (auto created = check(m_Device.core.CreateBufferView(m_Device.handle, &frameView, &view.frameView),
                                     "Create frame uniform view");
                !created)
            {
                return created;
            }
            m_Views.push_back(std::move(view));
        }
        m_Scene       = &scene;
        m_Environment = &build.environment;
        const VriBufferViewDesc transformView {instances.handle,
                                               VriDescriptorType_StructuredBuffer,
                                               VriFormat_Unknown,
                                               0,
                                               instances.desc.size};
        if (auto created = check(m_Device.core.CreateBufferView(m_Device.handle, &transformView, &m_TransformView),
                                 "Create primitive transform view");
            !created)
        {
            return created;
        }
        // Sampler 0 is the default; scene sampler i is stored at i + 1.
        m_MaterialSamplers.resize(scene.samplers.size() + 1);
        for (size_t i = 0; i < m_MaterialSamplers.size(); ++i)
        {
            const auto desc = i == 0 ? repeatSampler() : scene.samplers[i - 1];
            if (auto created = check(m_Device.core.CreateSampler(m_Device.handle, &desc, &m_MaterialSamplers[i]),
                                     "Create material sampler");
                !created)
            {
                return created;
            }
        }
        const auto            count = uint32_t(scene.materials.size());
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum    = count + viewCount;
        pool.textureMaxNum          = count * kMaterialTextureCount + kFrameBindings * viewCount;
        pool.samplerMaxNum          = count * kMaterialTextureCount + viewCount;
        pool.constantBufferMaxNum   = viewCount;
        pool.structuredBufferMaxNum = 2 * viewCount; // Primitive transforms and scene lights.
        if (auto created = check(m_Device.core.CreateDescriptorPool(m_Device.handle, &pool, &m_Pool),
                                 "Create forward shading pool");
            !created)
        {
            return created;
        }
        for (uint32_t view = 0; view < viewCount; ++view)
        {
            if (auto allocated =
                    check(m_Device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_Views[view].frameSet, 1),
                          "Allocate frame set");
                !allocated)
            {
                return allocated;
            }
        }
        m_MaterialSets.resize(count);
        if (count != 0)
        {
            if (auto allocated =
                    check(m_Device.core.AllocateDescriptorSets(m_Pool, m_Layout, 1, m_MaterialSets.data(), count),
                          "Allocate material sets");
                !allocated)
            {
                return allocated;
            }
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
                    return makeError("Invalid material sampler index");
                }
                const auto view = scene.materialTextures[material][slot]->view();
                if (!view)
                {
                    return std::unexpected(view.error());
                }
                textures[slot] = *view;
                samplers[slot] = m_MaterialSamplers[size_t(slots[slot].sampler + 1)];
                updates[slot]  = {&textures[slot], 1};
            }
            updates[kMaterialTextureCount] = {samplers, kMaterialTextureCount};
            m_Device.core.UpdateDescriptorRanges(m_MaterialSets[material], 0, kMaterialTextureCount + 1, updates);
        }
        return {};
    }

    std::expected<SceneResources, Error> ForwardShadingStage::addPasses(StageBuild& build)
    {
        // prepare() treats a bound scene as complete, so any failure leaves the stage unbound.
        const auto fail = [this](const Error& error)
        {
            releaseScene();
            return std::unexpected(error);
        };
        if (!build.view)
        {
            return fail(Error {"vultra.forward_shading is a view stage"});
        }
        const auto vertices  = findSceneResource(build.resources, kSceneVertices);
        const auto indices   = findSceneResource(build.resources, kSceneIndices);
        const auto instances = findSceneResource(build.resources, kSceneInstances);
        const auto lights    = findSceneResource(build.resources, kSceneLights);
        if (!vertices || !indices || !instances || !lights)
        {
            return fail(Error {"vultra.forward_shading needs the geometry buffers and scene.lights"});
        }
        if (*build.view == 0)
        {
            if (auto bound = bindScene(build, build.graph.getBuffer(*instances)); !bound)
            {
                return fail(bound.error());
            }
        }
        else if (!m_Scene)
        {
            return fail(Error {"vultra.forward_shading builds view 0 first"});
        }
        const auto size = build.views[*build.view].size;
        const auto hdr =
            build.graph.createTexture(build.graphName(kSceneHdr), colorTexture(size, VriFormat_RGBA16_SFLOAT));
        if (!hdr)
        {
            return fail(hdr.error());
        }
        auto depthDesc = depthTexture(size);
        depthDesc.usage |= VriTextureUsage_TransferSrc;
        const auto depth = build.graph.createTexture(build.graphName(kSceneDepth), depthDesc);
        if (!depth)
        {
            return fail(depth.error());
        }
        const auto  color         = *hdr;
        const auto  depthResource = *depth;
        const auto* view          = &m_Views[*build.view];
        const auto  skybox        = build.graph.addPass(
            build.graphName("Skybox"),
            {{color, Usage::eColorWrite, true}},
            [this, color, view](VriCommandBuffer* cmd, RenderGraph& resources) -> std::expected<void, Error>
            {
                auto&      target    = resources.getTexture(color);
                const auto colorView = target.view();
                if (!colorView)
                {
                    return std::unexpected(colorView.error());
                }
                const float clear[4] {0, 0, 0, 1};
                beginColorPass(m_Device, cmd, *colorView, {target.desc.width, target.desc.height}, clear);
                m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
                m_Device.core.CmdSetPipeline(cmd, m_Skybox->handle());
                m_Device.core.CmdSetDescriptorSet(cmd, 0, view->frameSet);
                const VriDrawDesc draw {3, 1, 0, 0};
                m_Device.core.CmdDraw(cmd, &draw);
                m_Device.core.CmdEndRendering(cmd);
                return {};
            });
        if (!skybox)
        {
            return fail(skybox.error());
        }
        const auto vertexBuffer = *vertices;
        const auto indexBuffer  = *indices;
        const auto forward      = build.graph.addPass(
            build.graphName("Forward"),
            {{color, Usage::eColorReadWrite},
                  {depthResource, Usage::eDepthWrite, true},
                  {vertexBuffer, Usage::eVertexBuffer},
                  {indexBuffer, Usage::eIndexBuffer},
                  {*instances, Usage::eStorageRead},
                  {*lights, Usage::eStorageRead}},
            [this, color, depthResource, view, vertexBuffer, indexBuffer](VriCommandBuffer* cmd, RenderGraph& resources)
                -> std::expected<void, Error>
            {
                auto&      target    = resources.getTexture(color);
                const auto colorView = target.view();
                if (!colorView)
                {
                    return std::unexpected(colorView.error());
                }
                const auto depthView = resources.getTexture(depthResource).view();
                if (!depthView)
                {
                    return std::unexpected(depthView.error());
                }
                const Extent      extent {target.desc.width, target.desc.height};
                VriAttachmentDesc colorAttachment {};
                colorAttachment.view    = *colorView;
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
                attachments.renderArea = {0, 0, extent.width, extent.height};
                attachments.layerNum   = 1;
                m_Device.core.CmdBeginRendering(cmd, &attachments);
                const VriViewport viewport {0, 0, float(extent.width), float(extent.height), 0, 1};
                const VriRect     scissor {0, 0, extent.width, extent.height};
                m_Device.core.CmdSetViewports(cmd, &viewport, 1);
                m_Device.core.CmdSetScissors(cmd, &scissor, 1);
                drawScene(cmd, *view, resources.getBuffer(vertexBuffer), resources.getBuffer(indexBuffer));
                m_Device.core.CmdEndRendering(cmd);
                return {};
            });
        if (!forward)
        {
            return fail(forward.error());
        }
        return SceneResources {{std::string(kSceneHdr), color}, {std::string(kSceneDepth), depthResource}};
    }

    void ForwardShadingStage::drawScene(VriCommandBuffer* cmd, const View& view, Buffer& vertices, Buffer& indices)
    {
        m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
        const VriVertexBufferBinding binding {vertices.handle, 0};
        m_Device.core.CmdSetVertexBuffers(cmd, 0, &binding, 1);
        m_Device.core.CmdSetIndexBuffer(cmd, indices.handle, 0, VriIndexType_UInt32);
        VriPipeline* activePipeline = nullptr;
        for (const uint32_t index : view.visible)
        {
            const auto&    primitive = m_Scene->primitives[index];
            const auto&    material  = m_Scene->materials[primitive.material];
            const uint32_t variant   = (material.doubleSided ? 1u : 0u) + (m_Scene->primitiveMirrored(index) ? 2u : 0u);
            auto*          pipeline  = m_Forward[variant]->handle();
            if (pipeline != activePipeline)
            {
                m_Device.core.CmdSetPipeline(cmd, pipeline);
                m_Device.core.CmdSetDescriptorSet(cmd, 0, view.frameSet);
                activePipeline = pipeline;
            }
            float normalMode = 0;
            if (material.normalTexture.image >= 0)
            {
                // Two-channel normal maps reconstruct Z in the shader.
                const auto format = m_Scene->materialTextures[primitive.material][2]->desc.format;
                normalMode        = format == VriFormat_BC5_UNORM || format == VriFormat_RG8_UNORM ? 2.0f : 1.0f;
            }
            const MaterialData parameters {
                material.baseColor,
                {material.baseMetalness, material.specularRoughness, material.specularIor, material.normalScale},
                {material.emissionColor * material.emissionLuminance, material.occlusionStrength},
                {material.coatWeight, material.coatRoughness, material.coatIor, material.specularWeight},
                {material.specularColor, material.baseDiffuseRoughness},
                {material.alphaCutoff, material.baseWeight, 0, normalMode},
                {0, 0, 0, 0},
                {index, 0, 0, 0}};
            m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
            m_Device.core.CmdSetDescriptorSet(cmd, 1, m_MaterialSets[primitive.material]);
            const VriDrawIndexedDesc draw {primitive.indexCount, 1, primitive.firstIndex, 0, 0};
            m_Device.core.CmdDrawIndexed(cmd, &draw);
        }
    }

    std::expected<void, Error> ForwardShadingStage::prepare(RenderGraph& graph, const StageFrame& frame)
    {
        if (!m_Scene || !frame.view || *frame.view >= frame.cameras.size() || *frame.view >= m_Views.size() ||
            !m_Views[*frame.view].frameSet)
        {
            return makeError("vultra.forward_shading prepares a view after addPasses built it");
        }
        auto&       view   = m_Views[*frame.view];
        const auto& camera = frame.cameras[*frame.view];
        const auto  lights = frame.lights;
        const auto  finite = [](glm::vec3 value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        };
        if (!finite(settings.ambientColor) || glm::any(glm::lessThan(settings.ambientColor, glm::vec3(0))) ||
            !std::isfinite(settings.environmentIntensity) || settings.environmentIntensity < 0)
        {
            return makeError("Forward shading ambient color and environment intensity must be nonnegative");
        }
        const auto& lightBuffer = frame.lightBuffer;
        FrameData   data {};
        data.lightConfig    = {float(lights.size()), float(lightBuffer.mainDirectional()), 0, 0};
        data.lightDirection = {0, 1, 0, 0};
        // The shader evaluates the first directional light through the main-light fields.
        if (lightBuffer.mainDirectional() >= 0)
        {
            const auto& main    = lightBuffer.lights()[size_t(lightBuffer.mainDirectional())];
            data.lightDirection = {glm::vec3(main.directionKind), main.colorIntensity.w};
            data.lightColor     = {glm::vec3(main.colorIntensity), 0};
        }
        data.viewProjection        = camera.projection * camera.view;
        data.inverseViewProjection = glm::inverse(data.viewProjection);
        data.view                  = camera.view;
        data.cameraPosition        = glm::inverse(camera.view)[3];
        data.lightColor.w          = settings.environmentIntensity;
        data.ambientColor          = {settings.ambientColor, 0};
        data.options               = {settings.ibl ? 1.0f : 0.0f,
                        settings.skybox ? 1.0f : 0.0f,
                        0,
                        float(m_Environment->specular->desc.mipNum - 1)};
        data.cameraClip = {camera.nearPlane, camera.farPlane, 0, float(settings.debugMode)};
        data.overrides  = {-1, -1, 0, 0};

        view.visible.clear();
        const auto planes = frustumPlanes(data.viewProjection);
        for (uint32_t index = 0; index < m_Scene->primitives.size(); ++index)
        {
            if (m_Scene->primitives[index].indexCount && intersects(m_Scene->primitiveBounds(index), planes))
            {
                view.visible.push_back(index);
            }
        }

        auto* mapped = m_Device.core.MapBuffer(view.frameBuffer->handle, 0, sizeof(data));
        if (!mapped)
        {
            return makeError("Map forward shading frame data");
        }
        std::memcpy(mapped, &data, sizeof(data));
        m_Device.core.UnmapBuffer(view.frameBuffer->handle);

        // The shared frame block declares shadow, HDR and G-buffer slots this stage never samples;
        // every slot still needs a valid view.
        static_cast<void>(graph);
        const std::array                            textures {m_Environment->radiance.get(),
                                   m_Environment->diffuse.get(),
                                   m_Environment->specular.get(),
                                   m_Environment->brdfLut.get(),
                                   m_OpenPbrLuts.get()};
        std::array<VriDescriptor*, textures.size()> views {};
        for (size_t i = 0; i < textures.size(); ++i)
        {
            const auto textureView = textures[i]->view();
            if (!textureView)
            {
                return std::unexpected(textureView.error());
            }
            views[i] = *textureView;
        }
        const VriDescriptor* descriptors[kFrameBindings] {};
        std::ranges::fill(descriptors, views[0]);
        descriptors[0]  = view.frameView;
        descriptors[6]  = views[1];
        descriptors[7]  = views[2];
        descriptors[8]  = views[3];
        descriptors[10] = m_EnvironmentSampler;
        descriptors[11] = views[4];
        descriptors[19] = m_TransformView;
        descriptors[20] = lightBuffer.view();
        VriDescriptorRangeUpdateDesc updates[kFrameBindings] {};
        for (uint32_t i = 0; i < kFrameBindings; ++i)
        {
            updates[i] = {&descriptors[i], 1};
        }
        m_Device.core.UpdateDescriptorRanges(view.frameSet, 0, kFrameBindings, updates);
        return {};
    }

    void ForwardShadingStage::pollShaders()
    {
        for (auto& pipeline : m_Forward)
        {
            pipeline->poll();
        }
        m_Skybox->poll();
    }

    uint32_t ForwardShadingStage::visiblePrimitives(uint32_t view) const
    {
        return view < m_Views.size() ? uint32_t(m_Views[view].visible.size()) : 0;
    }
} // namespace vultra
