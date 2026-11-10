#include <vultra/servers/rendering/builtin/tone_mapping_pass.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vultra
{
    namespace
    {
        const std::array entries {ShaderEntry {"screenVertex", VriShaderStage_Vertex},
                                  ShaderEntry {"fragmentMain", VriShaderStage_Fragment}};

        std::expected<void, std::string> validateParameters(std::span<const double> parameters)
        {
            if (parameters.size() != 3 || !std::isfinite(parameters[0]) ||
                std::abs(parameters[0]) > std::numeric_limits<float>::max() || !std::isfinite(parameters[1]) ||
                parameters[1] < 0 || parameters[1] > 1 || !std::isfinite(parameters[2]) || parameters[2] < 0 ||
                parameters[2] > 2 || std::trunc(parameters[2]) != parameters[2])
            {
                return std::unexpected(
                    "Tone mapping requires finite float exposure, bypass in [0,1] and operator 0, 1 or 2");
            }
            return {};
        }
    } // namespace

    ToneMappingPass::ToneMappingPass(Device& device, VriFormat format) :
        m_Device(device),
        m_Format(format)
    {
    }

    std::expected<std::unique_ptr<ToneMappingPass>, std::string>
    ToneMappingPass::create(Device& device, VriFormat format, const AssetSource* shaders)
    {
        auto pass = std::unique_ptr<ToneMappingPass>(new ToneMappingPass(device, format));
        if (auto initialized = pass->initializeExpected(shaders); !initialized)
        {
            return std::unexpected(initialized.error());
        }
        return pass;
    }

    std::expected<void, std::string> ToneMappingPass::initializeExpected(const AssetSource* shaders)
    {
        auto& device = m_Device;
        if (m_Format != VriFormat_RGBA8_UNORM && m_Format != VriFormat_BGRA8_UNORM &&
            m_Format != VriFormat_RGBA16_SFLOAT)
        {
            return std::unexpected("Tone mapping requires display UNORM or linear RGBA16_SFLOAT output");
        }
        ShaderCompileOptions options;
        options.entries.assign(entries.begin(), entries.end());
        options.includeDirectories = {"builtin/shaders"};
        auto pipeline              = ShaderPipeline::create(
            device,
            "builtin/shaders/passes/tone_mapping.slang",
            std::move(options),
            [this](const ShaderProgram& program, std::span<const VriShaderDesc> shaders)
            {
                return buildExpected(program, shaders);
            },
            "builtin/shaders",
            ".vultra/shaders",
            shaders);

        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        m_Pipeline = std::move(*pipeline);
        return {};
    }

    std::expected<VriPipeline*, std::string> ToneMappingPass::buildExpected(const ShaderProgram&           program,
                                                                            std::span<const VriShaderDesc> shaders)
    {
        if (m_Bindings)
        {
            // Reload callbacks must not publish a replacement owner before the pipeline transaction commits.
            if (auto compatible = m_Bindings->validateLayoutExpected(program, entries); !compatible)
            {
                return std::unexpected(compatible.error());
            }
        }
        else
        {
            auto bindings = ShaderBindings::create(m_Device, program, entries);
            if (!bindings)
            {
                return std::unexpected(bindings.error());
            }
            const auto reflected = (*bindings)->bindings();
            const auto input = std::ranges::find(reflected, std::string_view("hdrColor"), &ShaderResourceBinding::name);
            const auto* parameters = program.parameters.field("toneParameters");
            if (reflected.size() != 2 || (*bindings)->pushConstantSize() != 16 || input == reflected.end() ||
                input->pushConstant || input->type != VriDescriptorType_Texture || input->count != 1 || input->array ||
                input->viewType != VriTextureViewType_2D || !parameters)
            {
                return std::unexpected("Tone mapping shader requires hdrColor Texture2D and a 16-byte parameter block");
            }
            auto constants = ShaderUniformBlock::create(*parameters);
            if (!constants)
            {
                return std::unexpected(constants.error());
            }
            const std::array values {0.f, 0.f, 0.f, 0.f};
            const std::array writes {
                ShaderUniformWrite {"values", std::span(values), ShaderParameterKind::eVector, 1, 4}};
            if (auto packed = constants->setExpected(writes); !packed)
            {
                return std::unexpected("Tone mapping shader parameters: " + packed.error());
            }
            if (constants->data().size() != 16)
            {
                return std::unexpected("Tone mapping parameter payload must contain four floats");
            }
            // This branch is only initial construction; the Pass is not published until initializeExpected succeeds.
            m_Constants.emplace(std::move(*constants));
            m_Bindings = std::move(*bindings);
        }
        const VriColorAttachmentDesc color {.format = m_Format, .colorWriteMask = VriColorWrite_RGBA};
        VriGraphicsPipelineDesc      desc {};
        desc.pipelineCache           = m_Device.pipelineCache;
        desc.pipelineLayout          = m_Bindings->layout();
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
        const auto   status          = m_Device.core.CreateGraphicsPipeline(m_Device.handle, &desc, &pipeline);
        if (status != VriResult_Success || !pipeline)
        {
            if (pipeline)
            {
                m_Device.core.DestroyPipeline(pipeline);
            }
            return std::unexpected("Create tone mapping pipeline failed: " + std::to_string(status));
        }
        return pipeline;
    }

    ToneMappingPass::~ToneMappingPass() = default;

    std::expected<std::vector<RenderGraph::Resource>, std::string>
    ToneMappingPass::addPassesExpected(RenderGraph&                           graph,
                                       std::string_view                       name,
                                       std::span<const RenderGraph::Resource> inputs,
                                       std::span<const double>                parameters)
    {
        if (inputs.size() != 1 || &graph.device() != &m_Device || !graph.ownsResource(inputs.front()))
        {
            return std::unexpected("Tone mapping needs one graph-owned HDR input on its device");
        }
        if (auto valid = validateParameters(parameters); !valid)
        {
            return std::unexpected(valid.error());
        }
        const auto source = inputs.front();
        const auto  info   = graph.resourceInfo(source);
        const auto& desc   = info.textureDesc;
        if (!info.isTexture || (desc.format != VriFormat_RGBA16_SFLOAT && desc.format != VriFormat_RGBA32_SFLOAT) ||
            desc.type != VriTextureType_2D || desc.layerNum != 1 || desc.mipNum != 1 || desc.sampleNum != 1)
        {
            return std::unexpected("Tone mapping input must be a single-sample 2D RGBA16F/RGBA32F image");
        }
        const Extent                   size {desc.width, desc.height};
        RenderGraph::AppendTransaction transaction(graph);
        auto created = graph.createTextureExpected(std::string(name) + ".color", colorTexture(size, m_Format));
        if (!created)
        {
            return std::unexpected(created.error());
        }
        const auto       output = *created;
        const std::array uses {RenderGraph::Use {source, Usage::eSampled},
                               RenderGraph::Use {output, Usage::eColorWrite}};
        auto             added = graph.addPassExpected(
            std::string(name),
            uses,
            [this, source, output, size, parameters](auto* cmd, auto& current) -> std::expected<void, std::string>
            {
                if (auto valid = validateParameters(parameters); !valid)
                {
                    return std::unexpected(valid.error());
                }
                auto inputTexture = current.getTextureExpected(source);
                if (!inputTexture)
                {
                    return std::unexpected(inputTexture.error());
                }
                auto view = (*inputTexture)->viewExpected();
                if (!view)
                {
                    return std::unexpected(view.error());
                }
                auto outputTexture = current.getTextureExpected(output);
                if (!outputTexture)
                {
                    return std::unexpected(outputTexture.error());
                }
                auto outputView = (*outputTexture)->viewExpected();
                if (!outputView)
                {
                    return std::unexpected(outputView.error());
                }
                const std::array constants {float(parameters[0]),
                                            m_Format == VriFormat_RGBA16_SFLOAT ? 0.0f : 1.0f,
                                            float(parameters[1]),
                                            float(parameters[2])};
                if (constants != m_LastValues)
                {
                    const std::array writes {
                        ShaderUniformWrite {"values", std::span(constants), ShaderParameterKind::eVector, 1, 4}};
                    if (auto packed = m_Constants->setExpected(writes); !packed)
                    {
                        return std::unexpected(packed.error());
                    }
                    m_LastValues = constants;
                }
                m_Input.views[0] = *view;
                auto prepared    = m_Bindings->updateExpected(std::span(&m_Input, 1));
                m_Input.views[0] = nullptr;
                if (!prepared)
                {
                    return prepared;
                }
                // This pass fully initializes its output; LOAD would require an undeclared attachment read.
                const float clear[4] {};
                beginColorPass(m_Device, cmd, *outputView, size, clear);
                if (auto bound = m_Bindings->bindExpected(cmd, m_Pipeline->handle()); !bound)
                {
                    m_Device.core.CmdEndRendering(cmd);
                    return bound;
                }
                m_Device.core.CmdSetConstants(cmd, 0, m_Constants->data().data(), uint32_t(m_Constants->data().size()));
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
        transaction.commit();
        return std::vector<RenderGraph::Resource> {output};
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
                {},
                {},
                false,
                [](Device& device) -> std::expected<std::unique_ptr<GraphPass>, std::string>
                {
                    auto pass = ToneMappingPass::create(device);
                    if (!pass)
                    {
                        return std::unexpected(pass.error());
                    }
                    return std::move(*pass);
                }};
    }
} // namespace vultra
