#include "color_gain.hpp"

#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <array>
#include <stdexcept>

namespace research
{
    namespace
    {
        class ColorGain final : public vultra::GraphPass
        {
        public:
            explicit ColorGain(vultra::Device& device) :
                m_Device(device)
            {
                using namespace vultra;
                if (!(device.core.GetFormatSupport(device.handle, VriFormat_RGBA16_SFLOAT) &
                      VriFormatSupport_StorageTexture))
                {
                    throw std::runtime_error("Color gain requires RGBA16_SFLOAT storage textures");
                }
                try
                {
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
                    check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout), "Create gain layout");
                    VriDescriptorPoolDesc pool {};
                    pool.descriptorSetMaxNum  = 1;
                    pool.textureMaxNum        = 1;
                    pool.storageTextureMaxNum = 1;
                    check(device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool), "Create gain pool");
                    check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_Set, 1), "Allocate gain set");
                    m_Pipeline = std::make_unique<ShaderPipeline>(
                        device,
                        "examples/research/shaders/color_gain.slang",
                        std::vector<ShaderEntry> {{"gainMain", VriShaderStage_Compute}},
                        [this](std::span<const VriShaderDesc> shaders)
                        {
                            VriComputePipelineDesc desc {};
                            desc.pipelineCache    = m_Device.pipelineCache;
                            desc.pipelineLayout   = m_Layout;
                            desc.shader           = shaders.front();
                            VriPipeline* pipeline = nullptr;
                            check(m_Device.core.CreateComputePipeline(m_Device.handle, &desc, &pipeline),
                                  "Create gain compute pipeline");
                            return pipeline;
                        });
                }
                catch (...)
                {
                    release();
                    throw;
                }
            }

            ~ColorGain() override
            {
                release();
            }

            std::vector<vultra::RenderGraph::Resource> addPasses(vultra::RenderGraph&                           graph,
                                                                 std::string_view                               name,
                                                                 std::span<const vultra::RenderGraph::Resource> inputs,
                                                                 std::span<const double> parameters) override
            {
                using namespace vultra;
                const auto source = inputs.front();
                auto       desc   = graph.resourceInfo(source).textureDesc;
                desc.usage        = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource |
                             VriTextureUsage_TransferSrc;
                const auto output = graph.createTexture(std::string(name) + ".color", desc);
                graph.addPass(
                    std::string(name),
                    {{source, Usage::eSampled}, {output, Usage::eStorageWrite}},
                    [this, source, output, parameters, desc](auto* cmd, auto& resources)
                    {
                        const VriDescriptor*                        views[2] {resources.getTexture(source).view(),
                                                                              resources.getTexture(output).view()};
                        std::array<VriDescriptorRangeUpdateDesc, 2> updates {};
                        for (size_t i = 0; i < updates.size(); ++i)
                        {
                            updates[i].descriptors   = &views[i];
                            updates[i].descriptorNum = 1;
                        }
                        m_Device.core.UpdateDescriptorRanges(m_Set, 0, uint32_t(updates.size()), updates.data());
                        m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
                        m_Device.core.CmdSetPipeline(cmd, m_Pipeline->handle());
                        m_Device.core.CmdSetDescriptorSet(cmd, 0, m_Set);
                        const std::array constants {float(parameters.front()), 0.0f, 0.0f, 0.0f};
                        m_Device.core.CmdSetConstants(cmd, 0, constants.data(), sizeof(constants));
                        const VriDispatchDesc dispatch {(desc.width + 7) / 8, (desc.height + 7) / 8, 1};
                        m_Device.core.CmdDispatch(cmd, &dispatch);
                    });
                return {output};
            }

        private:
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

    vultra::PassDefinition colorGainDefinition()
    {
        return {"research.color_gain",
                {{"source", vultra::PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT}},
                {{"color", vultra::PassResourceKind::eTexture, VriFormat_RGBA16_SFLOAT, 0}},
                {{"gain", 1, 0, 8}},
                0,
                [](vultra::Device& device)
                {
                    return std::make_unique<ColorGain>(device);
                }};
    }
} // namespace research
