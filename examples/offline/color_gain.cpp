#include "color_gain.hpp"

#include <vultra/shader/shader_pipeline.hpp>

#include <array>

namespace research
{
    namespace
    {
        // The pipeline builder keeps this object's address, so it lives behind a unique_ptr.
        class ColorGain final : public vultra::GraphPass
        {
        public:
            static std::expected<std::unique_ptr<ColorGain>, vultra::Error> create(vultra::Device&              device,
                                                                                   const vultra::ShaderProgram* program)
            {
                using namespace vultra;
                if (!device.core.GetDeviceDesc(device.handle)->hasComputeShader ||
                    !(device.core.GetFormatSupport(device.handle, VriFormat_RGBA16_SFLOAT) &
                      VriFormatSupport_StorageTexture))
                {
                    return makeError("Color gain requires Compute and RGBA16_SFLOAT storage textures");
                }
                // The destructor releases whatever was created before a failure.
                std::unique_ptr<ColorGain> pass(new ColorGain(device));
                const std::array           ranges {
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
                if (auto created = check(device.core.CreatePipelineLayout(device.handle, &layout, &pass->m_Layout),
                                         "Create gain layout");
                    !created)
                {
                    return std::unexpected(created.error());
                }
                VriDescriptorPoolDesc pool {};
                pool.descriptorSetMaxNum  = 1;
                pool.textureMaxNum        = 1;
                pool.storageTextureMaxNum = 1;
                if (auto created = check(device.core.CreateDescriptorPool(device.handle, &pool, &pass->m_Pool),
                                         "Create gain pool");
                    !created)
                {
                    return std::unexpected(created.error());
                }
                if (auto allocated =
                        check(device.core.AllocateDescriptorSets(pass->m_Pool, pass->m_Layout, 0, &pass->m_Set, 1),
                              "Allocate gain set");
                    !allocated)
                {
                    return std::unexpected(allocated.error());
                }
                const auto build =
                    [self = pass.get()](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, Error>
                {
                    VriComputePipelineDesc desc {};
                    desc.pipelineCache    = self->m_Device.pipelineCache;
                    desc.pipelineLayout   = self->m_Layout;
                    desc.shader           = shaders.front();
                    VriPipeline* pipeline = nullptr;
                    if (auto created =
                            check(self->m_Device.core.CreateComputePipeline(self->m_Device.handle, &desc, &pipeline),
                                  "Create gain compute pipeline");
                        !created)
                    {
                        return std::unexpected(created.error());
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
                pass->m_Pipeline = std::move(*pipeline);
                return pass;
            }

            ~ColorGain() override
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

            std::expected<std::vector<vultra::RenderGraph::Resource>, vultra::Error>
            addPasses(vultra::RenderGraph&                           graph,
                      std::string_view                               name,
                      std::span<const vultra::RenderGraph::Resource> inputs,
                      std::span<const double>                        parameters) override
            {
                using namespace vultra;
                const auto source = inputs.front();
                const auto info   = graph.resourceInfo(source);
                if (!info)
                {
                    return std::unexpected(info.error());
                }
                auto desc          = info->textureDesc;
                desc.usage         = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_ShaderResource |
                                     VriTextureUsage_TransferSrc;
                const auto created = graph.createTexture(std::string(name) + ".color", desc);
                if (!created)
                {
                    return std::unexpected(created.error());
                }
                const auto output = *created;
                if (auto added = graph.addPass(
                        std::string(name),
                        {{source, Usage::eSampled}, {output, Usage::eStorageWrite}},
                        [this, source, output, parameters, desc](auto* cmd,
                                                                 auto& resources) -> std::expected<void, Error>
                        {
                            const auto sourceView = resources.getTexture(source).view();
                            if (!sourceView)
                            {
                                return std::unexpected(sourceView.error());
                            }
                            const auto outputView = resources.getTexture(output).view();
                            if (!outputView)
                            {
                                return std::unexpected(outputView.error());
                            }
                            const VriDescriptor*                        views[2] {*sourceView, *outputView};
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
                            return {};
                        });
                    !added)
                {
                    return std::unexpected(added.error());
                }
                return std::vector {output};
            }

        private:
            explicit ColorGain(vultra::Device& device) :
                m_Device(device)
            {
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
                [program](vultra::Device& device) -> std::expected<std::unique_ptr<vultra::GraphPass>, vultra::Error>
                {
                    return ColorGain::create(device, program);
                }};
    }
} // namespace research
