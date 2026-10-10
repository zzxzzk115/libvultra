#include <vultra/servers/rendering/builtin/tone_mapping_pass.hpp>

#include <array>

namespace vultra
{
    ToneMappingPass::ToneMappingPass(Device& device, VriFormat format) :
        m_Device(device),
        m_Format(format)
    {
        if (format != VriFormat_RGBA8_UNORM && format != VriFormat_BGRA8_UNORM && format != VriFormat_RGBA16_SFLOAT)
        {
            throw std::invalid_argument("Tone mapping requires display UNORM or linear RGBA16_SFLOAT output");
        }
        try
        {
            const VriDescriptorRangeDesc range {0, 1, VriDescriptorType_Texture, VriShaderStage_Fragment};
            const VriDescriptorSetDesc   set {0, &range, 1};
            const VriPushConstantDesc    push {0, 16, VriShaderStage_Fragment};
            VriPipelineLayoutDesc        layout {};
            layout.descriptorSets   = &set;
            layout.descriptorSetNum = 1;
            layout.pushConstants    = &push;
            layout.pushConstantNum  = 1;
            check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout), "Create tone mapping layout");
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum = 1;
            pool.textureMaxNum       = 1;
            check(device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool), "Create tone mapping pool");
            check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_Set, 1), "Allocate tone mapping set");
            m_Pipeline = std::make_unique<ShaderPipeline>(
                device,
                "builtin/shaders/passes/tone_mapping.slang",
                std::vector<ShaderEntry> {{"screenVertex", VriShaderStage_Vertex},
                                          {"fragmentMain", VriShaderStage_Fragment}},
                [this](std::span<const VriShaderDesc> shaders)
                {
                    const VriColorAttachmentDesc color {.format = m_Format, .colorWriteMask = VriColorWrite_RGBA};
                    VriGraphicsPipelineDesc      desc {};
                    desc.pipelineCache           = m_Device.pipelineCache;
                    desc.pipelineLayout          = m_Layout;
                    desc.shaders                 = shaders.data();
                    desc.shaderNum               = uint32_t(shaders.size());
                    desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode  = VriCullMode_None;
                    desc.rasterization.frontFace = VriFrontFace_CounterClockwise;
                    desc.rasterization.lineWidth = 1;
                    desc.multisample.sampleNum   = 1;
                    desc.outputMerger.colors     = &color;
                    desc.outputMerger.colorNum   = 1;
                    VriPipeline* pipeline        = nullptr;
                    check(m_Device.core.CreateGraphicsPipeline(m_Device.handle, &desc, &pipeline),
                          "Create tone mapping pipeline");
                    return pipeline;
                },
                "builtin/shaders",
                std::vector<std::filesystem::path> {"builtin/shaders"});
        }
        catch (...)
        {
            release();
            throw;
        }
    }

    ToneMappingPass::~ToneMappingPass()
    {
        release();
    }

    void ToneMappingPass::release()
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

    std::vector<RenderGraph::Resource> ToneMappingPass::addPasses(RenderGraph&                           graph,
                                                                  std::string_view                       name,
                                                                  std::span<const RenderGraph::Resource> inputs,
                                                                  std::span<const double>                parameters)
    {
        if (inputs.size() != 1 || parameters.size() != 3 || &graph.device() != &m_Device)
        {
            throw std::invalid_argument(
                "Tone mapping needs one HDR input and exposure/bypass/operator parameters on its device");
        }
        const auto source = inputs.front();
        const auto info   = graph.resourceInfo(source);
        if (!info.isTexture ||
            (info.textureDesc.format != VriFormat_RGBA16_SFLOAT && info.textureDesc.format != VriFormat_RGBA32_SFLOAT))
        {
            throw std::invalid_argument("Tone mapping input must be an RGBA16_SFLOAT or RGBA32_SFLOAT texture");
        }
        const Extent size {info.textureDesc.width, info.textureDesc.height};
        const auto   output = graph.createTexture(std::string(name) + ".color", colorTexture(size, m_Format));
        graph.addPass(std::string(name),
                      {{source, Usage::eSampled}, {output, Usage::eColorWrite}},
                      [this, source, output, size, parameters](auto* cmd, auto& current)
                      {
                          const VriDescriptor*               view = current.getTexture(source).view();
                          const VriDescriptorRangeUpdateDesc update {&view, 1};
                          m_Device.core.UpdateDescriptorRanges(m_Set, 0, 1, &update);
                          beginColorPass(m_Device, cmd, current.getTexture(output).view(), size);
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
                      });
        return {output};
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
                [](Device& device)
                {
                    return std::make_unique<ToneMappingPass>(device);
                }};
    }
} // namespace vultra
