#include <vultra/render/tone_mapping_pass.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace vultra
{
    ToneMappingPass::ToneMappingPass(Device& device, VriFormat format) :
        m_Device(device),
        m_Format(format)
    {
    }

    std::expected<std::unique_ptr<ToneMappingPass>, Error>
    ToneMappingPass::create(Device& device, VriFormat format, const AssetSource* shaders)
    {
        if (format != VriFormat_RGBA8_UNORM && format != VriFormat_BGRA8_UNORM && format != VriFormat_RGBA16_SFLOAT)
        {
            return makeError("Tone mapping requires display UNORM or linear RGBA16_SFLOAT output");
        }
        // The destructor releases whatever was created when a later step fails.
        std::unique_ptr<ToneMappingPass> pass(new ToneMappingPass(device, format));
        const VriDescriptorRangeDesc     range {0, 1, VriDescriptorType_Texture, VriShaderStage_Fragment};
        const VriDescriptorSetDesc       set {0, &range, 1};
        const VriPushConstantDesc        push {0, 16, VriShaderStage_Fragment};
        VriPipelineLayoutDesc            layout {};
        layout.descriptorSets   = &set;
        layout.descriptorSetNum = 1;
        layout.pushConstants    = &push;
        layout.pushConstantNum  = 1;
        if (auto created = check(device.core.CreatePipelineLayout(device.handle, &layout, &pass->m_Layout),
                                 "Create tone mapping layout");
            !created)
        {
            return std::unexpected(created.error());
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum = 1;
        pool.textureMaxNum       = 1;
        if (auto created = check(device.core.CreateDescriptorPool(device.handle, &pool, &pass->m_Pool),
                                 "Create tone mapping pool");
            !created)
        {
            return std::unexpected(created.error());
        }
        if (auto allocated = check(device.core.AllocateDescriptorSets(pass->m_Pool, pass->m_Layout, 0, &pass->m_Set, 1),
                                   "Allocate tone mapping set");
            !allocated)
        {
            return std::unexpected(allocated.error());
        }
        auto* self     = pass.get();
        auto  pipeline = ShaderPipeline::create(
            device,
            "builtin/shaders/passes/tone_mapping.slang",
            std::vector<ShaderEntry> {{"screenVertex", VriShaderStage_Vertex},
                                       {"fragmentMain", VriShaderStage_Fragment}},
            [self](std::span<const VriShaderDesc> stageShaders) -> std::expected<VriPipeline*, Error>
            {
                const VriColorAttachmentDesc color {.format = self->m_Format, .colorWriteMask = VriColorWrite_RGBA};
                VriGraphicsPipelineDesc      desc {};
                desc.pipelineCache           = self->m_Device.pipelineCache;
                desc.pipelineLayout          = self->m_Layout;
                desc.shaders                 = stageShaders.data();
                desc.shaderNum               = uint32_t(stageShaders.size());
                desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                desc.rasterization.cullMode  = VriCullMode_None;
                desc.rasterization.frontFace = VriFrontFace_CounterClockwise;
                desc.rasterization.lineWidth = 1;
                desc.multisample.sampleNum   = 1;
                desc.outputMerger.colors     = &color;
                desc.outputMerger.colorNum   = 1;
                VriPipeline* result          = nullptr;
                if (auto created =
                        check(self->m_Device.core.CreateGraphicsPipeline(self->m_Device.handle, &desc, &result),
                              "Create tone mapping pipeline");
                    !created)
                {
                    return std::unexpected(created.error());
                }
                return result;
            },
            "builtin/shaders",
            std::vector<std::filesystem::path> {"builtin/shaders"},
            shaders);
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        pass->m_Pipeline = std::move(*pipeline);
        return pass;
    }

    ToneMappingPass::~ToneMappingPass()
    {
        m_Pipeline.reset();
        if (m_Pool)
        {
            m_Device.core.DestroyDescriptorPool(m_Pool);
        }
        if (m_Layout)
        {
            m_Device.core.DestroyPipelineLayout(m_Layout);
        }
    }

    std::expected<std::vector<RenderGraph::Resource>, Error>
    ToneMappingPass::addPasses(RenderGraph&                           graph,
                               std::string_view                       name,
                               std::span<const RenderGraph::Resource> inputs,
                               std::span<const double>                parameters)
    {
        if (inputs.size() != 1 || parameters.size() != 3 || &graph.device() != &m_Device)
        {
            return makeError("Tone mapping needs one HDR input and exposure/bypass/operator parameters on its device");
        }
        const auto source = inputs.front();
        const auto info   = graph.resourceInfo(source);
        if (!info)
        {
            return std::unexpected(info.error());
        }
        if (!info->isTexture || (info->textureDesc.format != VriFormat_RGBA16_SFLOAT &&
                                 info->textureDesc.format != VriFormat_RGBA32_SFLOAT))
        {
            return makeError("Tone mapping input must be an RGBA16_SFLOAT or RGBA32_SFLOAT texture");
        }
        const Extent size {info->textureDesc.width, info->textureDesc.height};
        const auto   output = graph.createTexture(std::string(name) + ".color", colorTexture(size, m_Format));
        if (!output)
        {
            return std::unexpected(output.error());
        }
        const auto target = *output;
        const auto added =
            graph.addPass(std::string(name),
                          {{source, Usage::eSampled}, {target, Usage::eColorWrite}},
                          [this, source, target, size, parameters](VriCommandBuffer* cmd,
                                                                   RenderGraph& current) -> std::expected<void, Error>
                          {
                              const auto sourceView = current.getTexture(source).view();
                              if (!sourceView)
                              {
                                  return std::unexpected(sourceView.error());
                              }
                              const auto targetView = current.getTexture(target).view();
                              if (!targetView)
                              {
                                  return std::unexpected(targetView.error());
                              }
                              const VriDescriptor*               view = *sourceView;
                              const VriDescriptorRangeUpdateDesc update {&view, 1};
                              m_Device.core.UpdateDescriptorRanges(m_Set, 0, 1, &update);
                              // This pass fully initializes its output; LOAD would require an undeclared attachment
                              // read.
                              const float clear[4] {};
                              beginColorPass(m_Device, cmd, *targetView, size, clear);
                              m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
                              m_Device.core.CmdSetPipeline(cmd, m_Pipeline->handle());
                              m_Device.core.CmdSetDescriptorSet(cmd, 0, m_Set);
                              const std::array constants {float(parameters[0]),
                                                          m_Format == VriFormat_RGBA16_SFLOAT ? 0.0f : 1.0f,
                                                          float(parameters[1]),
                                                          float(parameters[2])};
                              m_Device.core.CmdSetConstants(cmd, 0, constants.data(), sizeof(constants));
                              const VriViewport viewport {0, 0, float(size.width), float(size.height), 0, 1};
                              const VriRect     scissor {0, 0, size.width, size.height};
                              m_Device.core.CmdSetViewports(cmd, &viewport, 1);
                              m_Device.core.CmdSetScissors(cmd, &scissor, 1);
                              const VriDrawDesc draw {3, 1, 0, 0};
                              m_Device.core.CmdDraw(cmd, &draw);
                              m_Device.core.CmdEndRendering(cmd);
                              return {};
                          });
        if (!added)
        {
            return std::unexpected(added.error());
        }
        return std::vector<RenderGraph::Resource> {target};
    }

    ShaderPipeline& ToneMappingPass::shader()
    {
        return *m_Pipeline;
    }

    PassDefinition toneMappingDefinition()
    {
        return {"vultra.tone_mapping",
                {{"hdr", PassResourceKind::eTexture, VriFormat_Unknown}},
                {{"color", PassResourceKind::eTexture, VriFormat_RGBA8_UNORM, 0}},
                {{"exposure", 0, -16, 16},
                 {"bypass", 0, 0, 1},
                 {"operator",
                  0,
                  0,
                  2,
                  "Tone operator",
                  "Display transform",
                  PassControl::eChoice,
                  {{"ACES", 0}, {"None", 1}, {"Reinhard", 2}}}},
                0,
                [](Device& device) -> std::expected<std::unique_ptr<GraphPass>, Error>
                {
                    auto pass = ToneMappingPass::create(device);
                    if (!pass)
                    {
                        return std::unexpected(pass.error());
                    }
                    return std::unique_ptr<GraphPass>(std::move(*pass));
                }};
    }
} // namespace vultra

namespace vultra
{
    namespace
    {
        class ToneMappingStage final : public RenderStage
        {
        public:
            explicit ToneMappingStage(const AssetSource* shaders) :
                m_Shaders(shaders)
            {
            }

            std::expected<SceneResources, Error> addPasses(StageBuild& build) override
            {
                if (!build.view)
                {
                    return makeError("vultra.tone_mapping is a view stage");
                }
                const auto input = findSceneResource(build.resources, kSceneOutput);
                if (!input)
                {
                    return std::unexpected(input.error());
                }
                // One pass per view: a pass rewrites its descriptor set when it records.
                if (*build.view == 0 && m_Format != build.displayFormat)
                {
                    m_Passes.clear();
                    m_Format = build.displayFormat;
                }
                while (m_Passes.size() <= *build.view)
                {
                    auto pass = ToneMappingPass::create(build.graph.device(), m_Format, m_Shaders);
                    if (!pass)
                    {
                        return std::unexpected(pass.error());
                    }
                    m_Passes.push_back(std::move(*pass));
                }
                const std::array inputs {*input};
                const auto       color = m_Passes[*build.view]->addPasses(build.graph,
                                                                    build.graphName("Tone mapping"),
                                                                    inputs,
                                                                    m_Parameters);
                if (!color)
                {
                    return std::unexpected(color.error());
                }
                return SceneResources {{std::string(kFinal), color->front()}};
            }

            std::expected<void, Error> prepare(RenderGraph&, const StageFrame& frame) override
            {
                if (frame.parameters.size() != m_Parameters.size())
                {
                    return makeError("vultra.tone_mapping takes exposure, bypass and operator");
                }
                std::ranges::copy(frame.parameters, m_Parameters.begin());
                return {};
            }

            void pollShaders() override
            {
                for (auto& pass : m_Passes)
                {
                    pass->shader().poll();
                }
            }

        private:
            const AssetSource*                            m_Shaders;
            VriFormat                                     m_Format = VriFormat_Unknown;
            std::vector<std::unique_ptr<ToneMappingPass>> m_Passes;
            // Read when the passes record, after prepare() set them for the frame.
            std::array<double, 3> m_Parameters {0, 0, 0};
        };
    } // namespace

    StageType toneMappingStage()
    {
        return {{"vultra.tone_mapping",
                 {std::string(kDisplaySlot)},
                 StageScope::eView,
                 {{std::string(kSceneOutput)}},
                 {{std::string(kFinal), VriFormat_Unknown, "color.display"}},
                 toneMappingDefinition().parameters},
                [](Device&, const AssetSource* shaders) -> std::expected<std::unique_ptr<RenderStage>, Error>
                {
                    return std::make_unique<ToneMappingStage>(shaders);
                }};
    }
} // namespace vultra
