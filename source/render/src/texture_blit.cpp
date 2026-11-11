#include <vultra/render/texture_blit.hpp>

#include <utility>

namespace vultra
{
    TextureBlit::TextureBlit(Device& device, uint32_t sourceCount) :
        m_Device(device),
        m_Sets(sourceCount),
        m_Sources(sourceCount)
    {
    }

    std::expected<std::unique_ptr<TextureBlit>, Error>
    TextureBlit::create(Device& device, VriFormat targetFormat, uint32_t sourceCount, const ShaderProgram* program)
    {
        if (!sourceCount)
        {
            return makeError("TextureBlit needs at least one source slot");
        }
        // The destructor releases whatever was created when a later step fails.
        std::unique_ptr<TextureBlit> blit(new TextureBlit(device, sourceCount));
        VriDescriptorRangeDesc       ranges[2] {{0, 1, VriDescriptorType_Texture, VriShaderStage_Fragment},
                                                {1, 1, VriDescriptorType_Sampler, VriShaderStage_Fragment}};
        VriDescriptorSetDesc         set {};
        set.ranges   = ranges;
        set.rangeNum = 2;
        VriPushConstantDesc   push {0, 32, VriShaderStage_Fragment};
        VriPipelineLayoutDesc layout {};
        layout.descriptorSets   = &set;
        layout.descriptorSetNum = 1;
        layout.pushConstants    = &push;
        layout.pushConstantNum  = 1;
        layout.shaderStages     = VriShaderStage_Vertex | VriShaderStage_Fragment;
        if (auto created =
                check(device.core.CreatePipelineLayout(device.handle, &layout, &blit->m_Layout), "Create blit layout");
            !created)
        {
            return std::unexpected(created.error());
        }
        VriSamplerDesc sampler {};
        sampler.minFilter    = VriFilter_Linear;
        sampler.magFilter    = VriFilter_Linear;
        sampler.addressModeU = VriAddressMode_ClampToEdge;
        sampler.addressModeV = VriAddressMode_ClampToEdge;
        sampler.addressModeW = VriAddressMode_ClampToEdge;
        if (auto created =
                check(device.core.CreateSampler(device.handle, &sampler, &blit->m_Sampler), "Create blit sampler");
            !created)
        {
            return std::unexpected(created.error());
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum = sourceCount;
        pool.textureMaxNum       = sourceCount;
        pool.samplerMaxNum       = sourceCount;
        if (auto created =
                check(device.core.CreateDescriptorPool(device.handle, &pool, &blit->m_Pool), "Create blit pool");
            !created)
        {
            return std::unexpected(created.error());
        }
        if (auto allocated = check(
                device.core.AllocateDescriptorSets(blit->m_Pool, blit->m_Layout, 0, blit->m_Sets.data(), sourceCount),
                "Allocate blit sets");
            !allocated)
        {
            return std::unexpected(allocated.error());
        }
        const std::vector<ShaderEntry> entries {{"screenVertex", VriShaderStage_Vertex},
                                                {"blitMain", VriShaderStage_Fragment}};
        auto*                          self = blit.get();
        const auto build = [self,
                            targetFormat](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, Error>
        {
            VriColorAttachmentDesc color {};
            color.format         = targetFormat;
            color.colorWriteMask = VriColorWrite_RGBA;
            VriGraphicsPipelineDesc desc {};
            desc.pipelineCache           = self->m_Device.pipelineCache;
            desc.pipelineLayout          = self->m_Layout;
            desc.shaders                 = shaders.data();
            desc.shaderNum               = uint32_t(shaders.size());
            desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
            desc.rasterization.cullMode  = VriCullMode_None;
            desc.rasterization.lineWidth = 1;
            desc.multisample.sampleNum   = 1;
            desc.outputMerger.colors     = &color;
            desc.outputMerger.colorNum   = 1;
            VriPipeline* result          = nullptr;
            if (auto created = check(self->m_Device.core.CreateGraphicsPipeline(self->m_Device.handle, &desc, &result),
                                     "Create blit pipeline");
                !created)
            {
                return std::unexpected(created.error());
            }
            return result;
        };
        auto pipeline = program ? ShaderPipeline::create(device, *program, entries, build) :
                                  ShaderPipeline::create(device,
                                                         "builtin/shaders/passes/texture_blit.slang",
                                                         entries,
                                                         build,
                                                         "builtin/shaders",
                                                         std::vector<std::filesystem::path> {"builtin/shaders"});
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        blit->m_Pipeline = std::move(*pipeline);
        return blit;
    }

    TextureBlit::~TextureBlit()
    {
        m_Pipeline.reset();
        if (m_Pool)
        {
            m_Device.core.DestroyDescriptorPool(m_Pool);
        }
        if (m_Sampler)
        {
            m_Device.core.DestroyDescriptor(m_Sampler);
        }
        if (m_Layout)
        {
            m_Device.core.DestroyPipelineLayout(m_Layout);
        }
    }

    std::expected<void, Error> TextureBlit::setSource(uint32_t slot, Texture& source)
    {
        if (slot >= m_Sets.size())
        {
            return makeError("TextureBlit slot {} exceeds its {} sources", slot, m_Sets.size());
        }
        const auto view = source.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        const VriDescriptor*         descriptors[2] {*view, m_Sampler};
        VriDescriptorRangeUpdateDesc updates[2] {};
        for (uint32_t i = 0; i < 2; ++i)
        {
            updates[i].descriptors   = &descriptors[i];
            updates[i].descriptorNum = 1;
        }
        m_Device.core.UpdateDescriptorRanges(m_Sets[slot], 0, 2, updates);
        m_Sources[slot] = &source;
        return {};
    }

    std::expected<void, Error> TextureBlit::draw(VriCommandBuffer*        cmd,
                                                 Texture&                 target,
                                                 VriRect                  rectangle,
                                                 uint32_t                 slot,
                                                 bool                     encodeSrgb,
                                                 std::optional<ImageView> view)
    {
        if (view)
        {
            if (auto valid = validateImageView(*view); !valid)
            {
                return valid;
            }
        }
        if (slot >= m_Sources.size())
        {
            return makeError("TextureBlit slot {} exceeds its {} sources", slot, m_Sources.size());
        }
        auto* source = m_Sources[slot];
        if (!source || source == &target)
        {
            return makeError("Blit requires a distinct source texture");
        }
        const auto targetView = target.view();
        if (!targetView)
        {
            return std::unexpected(targetView.error());
        }
        source->transition(cmd,
                           {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
        target.transition(cmd,
                          {VriAccess_ColorAttachmentRead | VriAccess_ColorAttachmentWrite,
                           VriLayout_ColorAttachment,
                           VriPipelineStage_ColorAttachmentOutput});
        beginColorPass(m_Device, cmd, *targetView, {target.desc.width, target.desc.height});
        const VriViewport viewport {float(rectangle.x),
                                    float(rectangle.y),
                                    float(rectangle.width),
                                    float(rectangle.height),
                                    0,
                                    1};
        m_Device.core.CmdSetViewports(cmd, &viewport, 1);
        m_Device.core.CmdSetScissors(cmd, &rectangle, 1);
        m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
        m_Device.core.CmdSetPipeline(cmd, m_Pipeline->handle());
        m_Device.core.CmdSetDescriptorSet(cmd, 0, m_Sets[slot]);

        struct Parameters
        {
            uint32_t encode;
            uint32_t channel;
            float    minimum;
            float    maximum;
            uint32_t remap;
            uint32_t padding[3];
        };

        static_assert(sizeof(Parameters) == 32);
        const auto       settings = view.value_or(ImageView {});
        const Parameters parameters {uint32_t(encodeSrgb),
                                     uint32_t(settings.channel),
                                     settings.minimum,
                                     settings.maximum,
                                     uint32_t(view.has_value()),
                                     {}};
        m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
        const VriDrawDesc draw {3, 1, 0, 0};
        m_Device.core.CmdDraw(cmd, &draw);
        m_Device.core.CmdEndRendering(cmd);
        return {};
    }
} // namespace vultra
