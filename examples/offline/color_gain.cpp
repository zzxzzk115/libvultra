#include "color_gain.hpp"

#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <array>
#include <cmath>

namespace research
{
    namespace
    {
        class ColorGain final : public vultra::GraphPass
        {
        public:
            static std::expected<std::unique_ptr<vultra::GraphPass>, std::string>
            create(vultra::Device& device, const vultra::ShaderProgram* program)
            {
                auto pass = std::unique_ptr<ColorGain>(new ColorGain(device));
                if (auto initialized = pass->initializeExpected(program); !initialized)
                {
                    return std::unexpected(initialized.error());
                }
                return pass;
            }

            ~ColorGain() override
            {
                release();
            }

            std::expected<std::vector<vultra::RenderGraph::Resource>, std::string>
            addPassesExpected(vultra::RenderGraph&                           graph,
                              std::string_view                               name,
                              std::span<const vultra::RenderGraph::Resource> inputs,
                              std::span<const double>                        parameters) override
            {
                using namespace vultra;
                if (&graph.device() != &m_Device || inputs.size() != 1 || parameters.size() != 1 ||
                    !graph.ownsResource(inputs.front()) || !std::isfinite(parameters.front()) ||
                    parameters.front() < 0 || parameters.front() > 8)
                {
                    return std::unexpected("Color gain requires its graph, one input and gain in [0,8]");
                }
                const auto source = inputs.front();
                const auto info   = graph.resourceInfo(source);
                auto       desc   = info.textureDesc;
                if (!info.isTexture || desc.format != VriFormat_RGBA16_SFLOAT || desc.type != VriTextureType_2D ||
                    desc.mipNum != 1 || desc.layerNum != 1 || desc.sampleNum != 1)
                {
                    return std::unexpected("Color gain requires a single-sample 2D RGBA16F input");
                }
                desc.usage = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource |
                             VriTextureUsage_TransferSrc;
                auto created = graph.createTextureExpected(std::string(name) + ".color", desc);
                if (!created)
                {
                    return std::unexpected(created.error());
                }
                const auto       output = *created;
                const std::array uses {RenderGraph::Use {source, Usage::eSampled},
                                       RenderGraph::Use {output, Usage::eStorageWrite}};
                auto             added = graph.addPassExpected(
                    std::string(name),
                    uses,
                    [this, source, output, parameters, desc](auto* cmd,
                                                             auto& resources) -> std::expected<void, std::string>
                    {
                        if (!std::isfinite(parameters.front()) || parameters.front() < 0 || parameters.front() > 8)
                        {
                            return std::unexpected("Color gain must be finite and in [0,8]");
                        }
                        auto inputTexture = resources.getTextureExpected(source);
                        if (!inputTexture)
                        {
                            return std::unexpected(inputTexture.error());
                        }
                        auto outputTexture = resources.getTextureExpected(output);
                        if (!outputTexture)
                        {
                            return std::unexpected(outputTexture.error());
                        }
                        auto inputView = (*inputTexture)->viewExpected();
                        if (!inputView)
                        {
                            return std::unexpected(inputView.error());
                        }
                        auto outputView = (*outputTexture)->viewExpected();
                        if (!outputView)
                        {
                            return std::unexpected(outputView.error());
                        }
                        const std::array<const VriDescriptor*, 2>         views {*inputView, *outputView};
                        const std::array<VriDescriptorRangeUpdateDesc, 2> updates {{{&views[0], 1}, {&views[1], 1}}};
                        m_Device.core.UpdateDescriptorRanges(m_Set, 0, uint32_t(updates.size()), updates.data());
                        m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
                        m_Device.core.CmdSetPipeline(cmd, m_Pipeline->handle());
                        m_Device.core.CmdSetDescriptorSet(cmd, 0, m_Set);
                        const std::array constants {float(parameters.front()), 0.0f, 0.0f, 0.0f};
                        m_Device.core.CmdSetConstants(cmd, 0, constants.data(), sizeof(constants));
                        const VriDispatchDesc dispatch {(desc.width + 7) / 8, (desc.height + 7) / 8, 1};
                        m_Device.core.CmdDispatch(cmd, &dispatch);
                        return {};
                    });
                if (!added)
                {
                    return std::unexpected(added.error());
                }
                return std::vector<RenderGraph::Resource> {output};
            }

        private:
            explicit ColorGain(vultra::Device& device) :
                m_Device(device)
            {
            }

            std::expected<void, std::string> initializeExpected(const vultra::ShaderProgram* program)
            {
                using namespace vultra;
                auto& device = m_Device;
                if (!device.core.GetDeviceDesc(device.handle)->hasComputeShader ||
                    !(device.core.GetFormatSupport(device.handle, VriFormat_RGBA16_SFLOAT) &
                      VriFormatSupport_StorageTexture))
                {
                    return std::unexpected("Color gain requires Compute and RGBA16_SFLOAT storage textures");
                }
                const std::array ranges {
                    VriDescriptorRangeDesc {0, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
                    VriDescriptorRangeDesc {1, 1, VriDescriptorType_StorageTexture, VriShaderStage_Compute}};
                VriDescriptorSetDesc set {};
                set.ranges   = ranges.data();
                set.rangeNum = uint32_t(ranges.size());
                VriPushConstantDesc   push {0, 16, VriShaderStage_Compute};
                VriPipelineLayoutDesc layout {};
                layout.descriptorSets   = &set;
                layout.descriptorSetNum = 1;
                layout.pushConstants    = &push;
                layout.pushConstantNum  = 1;
                layout.shaderStages     = VriShaderStage_Compute;
                if (const auto status = device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout);
                    status != VriResult_Success || !m_Layout)
                {
                    return std::unexpected("Create gain layout failed: " + std::to_string(status));
                }
                VriDescriptorPoolDesc pool {};
                pool.descriptorSetMaxNum  = 1;
                pool.textureMaxNum        = 1;
                pool.storageTextureMaxNum = 1;
                if (const auto status = device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool);
                    status != VriResult_Success || !m_Pool)
                {
                    return std::unexpected("Create gain pool failed: " + std::to_string(status));
                }
                if (const auto status = device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_Set, 1);
                    status != VriResult_Success || !m_Set)
                {
                    return std::unexpected("Allocate gain set failed: " + std::to_string(status));
                }
                const auto build =
                    [this](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    VriComputePipelineDesc desc {};
                    desc.pipelineCache    = m_Device.pipelineCache;
                    desc.pipelineLayout   = m_Layout;
                    desc.shader           = shaders.front();
                    VriPipeline* pipeline = nullptr;
                    if (const auto status = m_Device.core.CreateComputePipeline(m_Device.handle, &desc, &pipeline);
                        status != VriResult_Success)
                    {
                        if (pipeline)
                        {
                            m_Device.core.DestroyPipeline(pipeline);
                        }
                        return std::unexpected("Create gain compute pipeline failed: " + std::to_string(status));
                    }
                    return pipeline;
                };
                const std::vector<ShaderEntry> entries {{"gainMain", VriShaderStage_Compute}};
                auto                           pipeline =
                    program ?
                                                  ShaderPipeline::create(device, *program, entries, build) :
                                                  ShaderPipeline::create(device, "examples/offline/shaders/color_gain.slang", entries, build);
                if (!pipeline)
                {
                    return std::unexpected(pipeline.error());
                }
                m_Pipeline = std::move(*pipeline);
                return {};
            }

            void release()
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

            vultra::Device&                         m_Device;
            VriPipelineLayout*                      m_Layout = nullptr;
            VriDescriptorPool*                      m_Pool   = nullptr;
            VriDescriptorSet*                       m_Set    = nullptr;
            std::unique_ptr<vultra::ShaderPipeline> m_Pipeline;
        };
    } // namespace

    vultra::PassDefinition colorGainDefinition(const vultra::ShaderProgram* program)
    {
        return {"research.color_gain",
                {{"source", vultra::PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT}},
                {{"color", vultra::PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT, 0}},
                {{"gain", 1, 0, 8}},
                0,
                {},
                {},
                false,
                [program](vultra::Device& device)
                {
                    return ColorGain::create(device, program);
                }};
    }
} // namespace research
