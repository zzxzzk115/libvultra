#include <vultra/render/environment.hpp>
#include <vultra/render/forward_path.hpp>
#include <vultra/render/openpbr_luts.hpp>
#include <vultra/render/scene.hpp>
#include <vultra/render/tone_mapping_pass.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

namespace vultra
{
    namespace
    {
        // Keep these layouts in sync with builtin/shaders/resources/frame_block.slangh.
        struct FrameData
        {
            glm::mat4 viewProjection;
            glm::mat4 inverseViewProjection;
            glm::mat4 view;
            glm::vec4 cameraPosition;
            glm::vec4 lightDirection; // xyz to the main light, w intensity.
            glm::vec4 lightColor;     // rgb main light, w environment intensity.
            glm::vec4 ambientColor;
            glm::vec4 options;     // IBL, skybox, unused, specular mip count - 1.
            glm::vec4 cameraClip;  // near, far, unused, debug mode.
            glm::vec4 overrides;   // Negative roughness/metalness keep materials.
            glm::vec4 lightConfig; // Count, main directional index (-1 if absent).
        };

        static_assert(offsetof(FrameData, lightConfig) == 304 && sizeof(FrameData) == 320);
        static_assert(sizeof(SceneVertex) == 64 && offsetof(SceneVertex, tangent) == 48);

        // The bindings of frame_block.slangh and openpbr_luts.slangh in set 0, in range order: frame constants,
        // camera constants, radiance, diffuse, specular, BRDF LUT, environment sampler, OpenPBR tables,
        // instances, scene lights.
        constexpr VriShaderStageFlags                    kFrameStages = VriShaderStage_Vertex | VriShaderStage_Fragment;
        constexpr std::array<VriDescriptorRangeDesc, 10> kFrameRanges {{
            {0, 1, VriDescriptorType_ConstantBuffer, kFrameStages},
            {1, 1, VriDescriptorType_ConstantBuffer, kFrameStages},
            {5, 1, VriDescriptorType_Texture, kFrameStages},
            {6, 1, VriDescriptorType_Texture, kFrameStages},
            {7, 1, VriDescriptorType_Texture, kFrameStages},
            {8, 1, VriDescriptorType_Texture, kFrameStages},
            {10, 1, VriDescriptorType_Sampler, kFrameStages},
            // The OpenPBR table is RGBA32F: WebGPU samples it as an unfilterable float without float32-filterable.
            {11, 1, VriDescriptorType_Texture, kFrameStages, 0, VriTextureViewType_2D, VriFormat_RGBA32_SFLOAT},
            {19, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Vertex},
            {20, 1, VriDescriptorType_StructuredBuffer, VriShaderStage_Fragment},
        }};
        constexpr uint32_t                               kFrameTextureCount = 5;

        // scene.hdr and the G-buffer layers of `api`, in the order of the shaders' GBufferOutput.
        std::vector<SceneResourceDesc> forwardTargets(VriGraphicsAPI api)
        {
            std::vector<SceneResourceDesc> targets {
                {std::string(kSceneHdr), VriFormat_RGBA16_SFLOAT, "color.linear.hdr"}};
            const auto layers = gbufferLayers(api);
            targets.insert(targets.end(), layers.begin(), layers.end());
            return targets;
        }

        // scene.hdr and the full profile's seven layers.
        constexpr size_t kMaxTargets = 8;

        std::expected<VriPipeline*, Error> createPipeline(Device&                            device,
                                                          VriPipelineLayout*                 layout,
                                                          std::span<const VriShaderDesc>     shaders,
                                                          bool                               mesh,
                                                          bool                               doubleSided,
                                                          bool                               mirrored,
                                                          std::span<const SceneResourceDesc> targets)
        {
            VriVertexStreamDesc    stream {sizeof(SceneVertex), 0, VriVertexStepRate_PerVertex};
            VriVertexAttributeDesc attributes[5] {};
            attributes[0] = {.format = VriFormat_RGB32_SFLOAT, .offset = offsetof(SceneVertex, position)};
            attributes[1] = {.format = VriFormat_RGB32_SFLOAT, .offset = offsetof(SceneVertex, normal)};
            attributes[2] = {.format = VriFormat_RG32_SFLOAT, .offset = offsetof(SceneVertex, uv)};
            attributes[3] = {.format = VriFormat_RGBA32_SFLOAT, .offset = offsetof(SceneVertex, color)};
            attributes[4] = {.format = VriFormat_RGBA32_SFLOAT, .offset = offsetof(SceneVertex, tangent)};
            std::array<VriColorAttachmentDesc, kMaxTargets> colors {};
            for (size_t index = 0; index < targets.size(); ++index)
            {
                colors[index].format         = targets[index].format;
                colors[index].colorWriteMask = VriColorWrite_RGBA;
            }
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
            desc.outputMerger.colors     = colors.data();
            desc.outputMerger.colorNum   = uint32_t(targets.size());
            VriPipeline* result          = nullptr;
            if (auto created = check(device.core.CreateGraphicsPipeline(device.handle, &desc, &result),
                                     "Create forward path pipeline");
                !created)
            {
                return std::unexpected(created.error());
            }
            return result;
        }

        // Begins rendering into `targets` and `depth` over the whole extent. Targets are cleared to zero when
        // `clearTargets` is set and kept otherwise; depth is cleared to the far plane.
        void beginTargets(Device&                         device,
                          VriCommandBuffer*               cmd,
                          std::span<VriDescriptor* const> targets,
                          VriDescriptor*                  depth,
                          Extent                          extent,
                          bool                            clearTargets)
        {
            std::array<VriAttachmentDesc, kMaxTargets> colors {};
            for (size_t index = 0; index < targets.size(); ++index)
            {
                colors[index].view    = targets[index];
                colors[index].loadOp  = clearTargets ? VriAttachmentLoadOp_Clear : VriAttachmentLoadOp_Load;
                colors[index].storeOp = VriAttachmentStoreOp_Store;
            }
            VriAttachmentDesc depthAttachment {};
            depthAttachment.view                          = depth;
            depthAttachment.loadOp                        = VriAttachmentLoadOp_Clear;
            depthAttachment.storeOp                       = VriAttachmentStoreOp_Store;
            depthAttachment.clearValue.depthStencil.depth = 1;
            VriAttachmentsDesc attachments {};
            attachments.colors     = colors.data();
            attachments.colorNum   = uint32_t(targets.size());
            attachments.depth      = depth ? &depthAttachment : nullptr;
            attachments.renderArea = {0, 0, extent.width, extent.height};
            attachments.layerNum   = 1;
            device.core.CmdBeginRendering(cmd, &attachments);
            const VriViewport viewport {0, 0, float(extent.width), float(extent.height), 0, 1};
            const VriRect     scissor {0, 0, extent.width, extent.height};
            device.core.CmdSetViewports(cmd, &viewport, 1);
            device.core.CmdSetScissors(cmd, &scissor, 1);
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
                const std::array<std::pair<std::string_view, Buffer*>, 6> buffers {
                    std::pair {kSceneVertices, build.scene.vertices.get()},
                    std::pair {kSceneIndices, build.scene.indices.get()},
                    std::pair {kSceneInstances, build.scene.instanceData.get()},
                    std::pair {kSceneModels, build.scene.modelData.get()},
                    std::pair {kSceneMaterials, build.scene.materialHeaders.get()},
                    std::pair {kSceneMaterialData, build.scene.materialData.get()}};
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
                  bufferResource(kSceneInstances, sizeof(InstanceData)),
                  bufferResource(kSceneModels, sizeof(ModelData)),
                  bufferResource(kSceneMaterials, sizeof(MaterialHeader)),
                  bufferResource(kSceneMaterialData, sizeof(uint32_t))}},
                [](Device&, const StageSources&) -> std::expected<std::unique_ptr<RenderStage>, Error>
                {
                    return std::make_unique<MeshStage>();
                }};
    }

    StageType forwardShadingStage(VriGraphicsAPI api, const AssetSource* materialShaders)
    {
        // The description is checked before a device exists, so the caller names the API it describes.
        auto provides = forwardTargets(api);
        provides.push_back({std::string(kSceneDepth), VriFormat_D32_SFLOAT, "depth"});
        return {{std::string(ForwardShadingStage::kName),
                 {"shading"},
                 StageScope::eView,
                 {{std::string(kSceneVertices)},
                  {std::string(kSceneIndices)},
                  {std::string(kSceneInstances)},
                  {std::string(kSceneMaterialData)},
                  {std::string(kSceneLights)},
                  {std::string(kCameraData)}},
                 std::move(provides)},
                [materialShaders](Device&             device,
                                  const StageSources& sources) -> std::expected<std::unique_ptr<RenderStage>, Error>
                {
                    auto stage = ForwardShadingStage::create(device, sources.shaders, materialShaders);
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
                {{"geometry", "vultra.mesh"},
                 {"shading", std::string(ForwardShadingStage::kName)},
                 {std::string(kDisplaySlot), "vultra.tone_mapping"}}};
    }

    std::expected<std::unique_ptr<RenderPath>, Error> createForwardPath(Device& device, const AssetSource* shaders)
    {
        const std::array stages {meshStage(), forwardShadingStage(device.graphicsAPI()), toneMappingStage()};
        return RenderPath::create(device, forwardPathDesc(), stages, {shaders});
    }

    ForwardShadingStage::ForwardShadingStage(Device& device) :
        m_Device(device)
    {
    }

    std::expected<std::unique_ptr<ForwardShadingStage>, Error>
    ForwardShadingStage::create(Device& device, const AssetSource* shaders, const AssetSource* materialShaders)
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

        const auto            materialRanges = SceneMaterials::ranges();
        VriDescriptorSetDesc  sets[2] {{0, kFrameRanges.data(), uint32_t(kFrameRanges.size())},
                                       {SceneMaterials::kSet, materialRanges.data(), uint32_t(materialRanges.size())}};
        const auto            push = SceneMaterials::constants();
        VriPipelineLayoutDesc layout {};
        layout.descriptorSets   = sets;
        layout.descriptorSetNum = 2;
        layout.pushConstants    = &push;
        layout.pushConstantNum  = 1;
        layout.shaderStages     = kFrameStages;
        if (auto created = check(device.core.CreatePipelineLayout(device.handle, &layout, &stage->m_Layout),
                                 "Create forward shading layout");
            !created)
        {
            return std::unexpected(created.error());
        }
        // The sky reads only the frame, and WebGPU draws only with every set of the layout bound.
        VriPipelineLayoutDesc skyLayout {};
        skyLayout.descriptorSets   = sets;
        skyLayout.descriptorSetNum = 1;
        skyLayout.shaderStages     = kFrameStages;
        if (auto created = check(device.core.CreatePipelineLayout(device.handle, &skyLayout, &stage->m_SkyLayout),
                                 "Create sky layout");
            !created)
        {
            return std::unexpected(created.error());
        }

        stage->m_Shaders         = shaders;
        stage->m_MaterialShaders = materialShaders;
        if (auto created = stage->createPipelines(false); !created)
        {
            return std::unexpected(created.error());
        }
        return stage;
    }

    std::expected<void, Error> ForwardShadingStage::createPipelines(bool gbuffer)
    {
        auto&      pipelines = m_Pipelines[gbuffer ? 1 : 0];
        const auto pipeline  = [this, gbuffer](const char* file, bool mesh, bool doubleSided, bool mirrored)
        {
            const char* vertex = "screenVertex";
            if (mesh)
            {
                vertex = gbuffer ? "vertexGBuffer" : "vertexMain";
            }
            return ShaderPipeline::create(
                m_Device,
                std::filesystem::path("builtin/shaders/passes") / file,
                std::vector<ShaderEntry> {{vertex, VriShaderStage_Vertex},
                                          {gbuffer ? "fragmentGBuffer" : "fragmentMain", VriShaderStage_Fragment}},
                [this, mesh, doubleSided, mirrored, gbuffer](std::span<const VriShaderDesc> stageShaders)
                {
                    const auto targets = forwardTargets(m_Device.core.GetDeviceDesc(m_Device.handle)->graphicsAPI);
                    return createPipeline(m_Device,
                                          mesh ? m_Layout : m_SkyLayout,
                                          stageShaders,
                                          mesh,
                                          doubleSided,
                                          mirrored,
                                          std::span(targets).first(gbuffer ? targets.size() : 1));
                },
                "builtin/shaders",
                std::vector<std::filesystem::path> {"builtin/shaders", "external"},
                mesh && m_MaterialShaders ? m_MaterialShaders : m_Shaders);
        };
        for (uint32_t variant = 0; variant < pipelines.forward.size(); ++variant)
        {
            auto forward = pipeline("forward.slang", true, (variant & 1) != 0, (variant & 2) != 0);
            if (!forward)
            {
                return std::unexpected(forward.error());
            }
            pipelines.forward[variant] = std::move(*forward);
        }
        auto skybox = pipeline("skybox.slang", false, true, false);
        if (!skybox)
        {
            return std::unexpected(skybox.error());
        }
        pipelines.skybox = std::move(*skybox);
        return {};
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
            if (view.cameraView)
            {
                m_Device.core.DestroyDescriptor(view.cameraView);
            }
        }
        m_Views.clear();
        for (auto& pipelines : m_Pipelines)
        {
            pipelines = {};
        }
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
        if (m_SkyLayout)
        {
            m_Device.core.DestroyPipelineLayout(m_SkyLayout);
            m_SkyLayout = nullptr;
        }
    }

    void ForwardShadingStage::releaseScene()
    {
        if (m_Pool)
        {
            m_Device.core.DestroyDescriptorPool(m_Pool);
            m_Pool = nullptr;
        }
        m_Materials.reset();
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
        auto materials = SceneMaterials::create(m_Device, m_Layout, scene);
        if (!materials)
        {
            return std::unexpected(materials.error());
        }
        m_Materials = std::move(*materials);
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum    = viewCount;
        pool.textureMaxNum          = kFrameTextureCount * viewCount;
        pool.samplerMaxNum          = viewCount;
        pool.constantBufferMaxNum   = 2 * viewCount; // Frame and camera constants.
        pool.structuredBufferMaxNum = 2 * viewCount; // Instances and scene lights.
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
        const auto materials = findSceneResource(build.resources, kSceneMaterialData);
        if (!vertices || !indices || !instances || !lights || !materials)
        {
            return fail(
                Error {"vultra.forward_shading needs the geometry buffers, scene.material_data and scene.lights"});
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
        const auto camera = findSceneResource(build.resources, kCameraData);
        if (!camera)
        {
            return fail(camera.error());
        }
        auto& view = m_Views[*build.view];
        if (view.cameraView)
        {
            m_Device.core.DestroyDescriptor(view.cameraView);
            view.cameraView = nullptr;
        }
        const VriBufferViewDesc cameraView {build.graph.getBuffer(*camera).handle,
                                            VriDescriptorType_ConstantBuffer,
                                            VriFormat_Unknown,
                                            0,
                                            sizeof(CameraData)};
        if (auto created = check(m_Device.core.CreateBufferView(m_Device.handle, &cameraView, &view.cameraView),
                                 "Create camera constants view");
            !created)
        {
            return fail(created.error());
        }
        // The G-buffer layers are written only when someone reads one of them; then all of them are.
        if (*build.view == 0)
        {
            const auto layers = gbufferLayers(m_Device.core.GetDeviceDesc(m_Device.handle)->graphicsAPI);
            m_GBuffer         = std::any_of(layers.begin(),
                                    layers.end(),
                                    [&build](const SceneResourceDesc& layer)
                                    {
                                        return build.requests(layer.name);
                                    });
            // The G-buffer pipelines exceed some devices' color attachment limits, so they exist only when used.
            if (m_GBuffer && !m_Pipelines[1].skybox)
            {
                if (auto created = createPipelines(true); !created)
                {
                    return fail(created.error());
                }
            }
        }
        const auto size = build.size;
        const auto all  = forwardTargets(m_Device.core.GetDeviceDesc(m_Device.handle)->graphicsAPI);
        const auto used = std::span(all).first(m_GBuffer ? all.size() : 1);
        std::vector<RenderGraph::Resource> targets;
        SceneResources                     provided;
        for (const auto& target : used)
        {
            const auto texture =
                build.graph.createTexture(build.graphName(target.name), colorTexture(size, target.format));
            if (!texture)
            {
                return fail(texture.error());
            }
            targets.push_back(*texture);
            provided.push_back({std::string(target.name), *texture});
        }
        auto depthDesc = depthTexture(size);
        depthDesc.usage |= VriTextureUsage_TransferSrc;
        const auto depth = build.graph.createTexture(build.graphName(kSceneDepth), depthDesc);
        if (!depth)
        {
            return fail(depth.error());
        }
        provided.push_back({std::string(kSceneDepth), *depth});
        const auto  depthResource = *depth;
        const auto* frameView     = &view;
        const auto* pipelines     = &m_Pipelines[m_GBuffer ? 1 : 0];
        // Views of the targets in GBufferOutput order.
        const auto targetViews = [targets](RenderGraph& resources) -> std::expected<std::vector<VriDescriptor*>, Error>
        {
            std::vector<VriDescriptor*> views;
            for (const auto target : targets)
            {
                const auto targetView = resources.getTexture(target).view();
                if (!targetView)
                {
                    return std::unexpected(targetView.error());
                }
                views.push_back(*targetView);
            }
            return views;
        };
        std::vector<RenderGraph::Use> skyUses;
        for (const auto target : targets)
        {
            skyUses.push_back({target, Usage::eColorWrite, true});
        }
        skyUses.push_back({*camera, Usage::eConstantBuffer});
        const auto skybox = build.graph.addPass(
            build.graphName("Skybox"),
            std::move(skyUses),
            [this, targets, targetViews, frameView, pipelines](VriCommandBuffer* cmd,
                                                               RenderGraph& resources) -> std::expected<void, Error>
            {
                const auto views = targetViews(resources);
                if (!views)
                {
                    return std::unexpected(views.error());
                }
                const auto& target = resources.getTexture(targets.front());
                beginTargets(m_Device, cmd, *views, nullptr, {target.desc.width, target.desc.height}, true);
                m_Device.core.CmdSetPipelineLayout(cmd, m_SkyLayout);
                m_Device.core.CmdSetPipeline(cmd, pipelines->skybox->handle());
                m_Device.core.CmdSetDescriptorSet(cmd, 0, frameView->frameSet);
                const VriDrawDesc draw {3, 1, 0, 0};
                m_Device.core.CmdDraw(cmd, &draw);
                m_Device.core.CmdEndRendering(cmd);
                return {};
            });
        if (!skybox)
        {
            return fail(skybox.error());
        }
        const auto                    vertexBuffer = *vertices;
        const auto                    indexBuffer  = *indices;
        std::vector<RenderGraph::Use> forwardUses;
        for (const auto target : targets)
        {
            forwardUses.push_back({target, Usage::eColorReadWrite});
        }
        forwardUses.push_back({depthResource, Usage::eDepthWrite, true});
        forwardUses.push_back({vertexBuffer, Usage::eVertexBuffer});
        forwardUses.push_back({indexBuffer, Usage::eIndexBuffer});
        forwardUses.push_back({*instances, Usage::eStorageRead});
        forwardUses.push_back({*materials, Usage::eStorageRead});
        forwardUses.push_back({*lights, Usage::eStorageRead});
        forwardUses.push_back({*camera, Usage::eConstantBuffer});
        const auto forward = build.graph.addPass(
            build.graphName("Forward"),
            std::move(forwardUses),
            [this, targets, targetViews, depthResource, frameView, pipelines, vertexBuffer, indexBuffer](
                VriCommandBuffer* cmd,
                RenderGraph&      resources) -> std::expected<void, Error>
            {
                const auto views = targetViews(resources);
                if (!views)
                {
                    return std::unexpected(views.error());
                }
                const auto depthView = resources.getTexture(depthResource).view();
                if (!depthView)
                {
                    return std::unexpected(depthView.error());
                }
                const auto& target = resources.getTexture(targets.front());
                beginTargets(m_Device, cmd, *views, *depthView, {target.desc.width, target.desc.height}, false);
                const std::array forwardPipelines {pipelines->forward[0]->handle(),
                                                   pipelines->forward[1]->handle(),
                                                   pipelines->forward[2]->handle(),
                                                   pipelines->forward[3]->handle()};
                m_Materials->draw(cmd,
                                  resources.getBuffer(vertexBuffer),
                                  resources.getBuffer(indexBuffer),
                                  frameView->visible,
                                  forwardPipelines,
                                  frameView->frameSet);
                m_Device.core.CmdEndRendering(cmd);
                return {};
            });
        if (!forward)
        {
            return fail(forward.error());
        }
        return provided;
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

        m_Materials->cull(data.viewProjection, view.visible);

        auto* mapped = m_Device.core.MapBuffer(view.frameBuffer->handle, 0, sizeof(data));
        if (!mapped)
        {
            return makeError("Map forward shading frame data");
        }
        std::memcpy(mapped, &data, sizeof(data));
        m_Device.core.UnmapBuffer(view.frameBuffer->handle);

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
        // In the order of kFrameRanges.
        const std::array<const VriDescriptor*, kFrameRanges.size()>   descriptors {view.frameView,
                                                                                 view.cameraView,
                                                                                 views[0],
                                                                                 views[1],
                                                                                 views[2],
                                                                                 views[3],
                                                                                 m_EnvironmentSampler,
                                                                                 views[4],
                                                                                 m_TransformView,
                                                                                 lightBuffer.view()};
        std::array<VriDescriptorRangeUpdateDesc, kFrameRanges.size()> updates {};
        for (size_t i = 0; i < updates.size(); ++i)
        {
            updates[i] = {&descriptors[i], 1};
        }
        m_Device.core.UpdateDescriptorRanges(view.frameSet, 0, uint32_t(updates.size()), updates.data());
        return {};
    }

    void ForwardShadingStage::pollShaders()
    {
        for (auto& pipelines : m_Pipelines)
        {
            if (!pipelines.skybox)
            {
                continue;
            }
            for (auto& pipeline : pipelines.forward)
            {
                pipeline->poll();
            }
            pipelines.skybox->poll();
        }
    }

    uint32_t ForwardShadingStage::visiblePrimitives(uint32_t view) const
    {
        return view < m_Views.size() ? uint32_t(m_Views[view].visible.size()) : 0;
    }
} // namespace vultra
