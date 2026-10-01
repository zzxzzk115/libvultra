#include <vultra/servers/rendering/texture_blit.hpp>

namespace vultra
{
    TextureBlit::TextureBlit(Device& device, VriFormat targetFormat, uint32_t sourceCount) :
        m_Device(device),
        m_Sets(sourceCount),
        m_Sources(sourceCount)
    {
        if (!sourceCount)
        {
            throw std::invalid_argument("TextureBlit needs at least one source slot");
        }
        try
        {
            VriDescriptorRangeDesc ranges[2] {{0, 1, VriDescriptorType_Texture, VriShaderStage_Fragment},
                                              {1, 1, VriDescriptorType_Sampler, VriShaderStage_Fragment}};
            VriDescriptorSetDesc   set {};
            set.ranges   = ranges;
            set.rangeNum = 2;
            VriPushConstantDesc   push {0, 16, VriShaderStage_Fragment};
            VriPipelineLayoutDesc layout {};
            layout.descriptorSets   = &set;
            layout.descriptorSetNum = 1;
            layout.pushConstants    = &push;
            layout.pushConstantNum  = 1;
            layout.shaderStages     = VriShaderStage_Vertex | VriShaderStage_Fragment;
            check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout), "Create blit layout");
            VriSamplerDesc sampler {};
            sampler.minFilter    = VriFilter_Linear;
            sampler.magFilter    = VriFilter_Linear;
            sampler.addressModeU = VriAddressMode_ClampToEdge;
            sampler.addressModeV = VriAddressMode_ClampToEdge;
            sampler.addressModeW = VriAddressMode_ClampToEdge;
            check(device.core.CreateSampler(device.handle, &sampler, &m_Sampler), "Create blit sampler");
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum = sourceCount;
            pool.textureMaxNum       = sourceCount;
            pool.samplerMaxNum       = sourceCount;
            check(device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool), "Create blit pool");
            check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, m_Sets.data(), sourceCount),
                  "Allocate blit sets");
            m_Pipeline = std::make_unique<ShaderPipeline>(
                device,
                "builtin/shaders/passes/texture_blit.slang",
                std::vector<ShaderEntry> {{"screenVertex", VriShaderStage_Vertex},
                                          {"blitMain", VriShaderStage_Fragment}},
                [this, targetFormat](std::span<const VriShaderDesc> shaders)
                {
                    VriColorAttachmentDesc color {};
                    color.format         = targetFormat;
                    color.colorWriteMask = VriColorWrite_RGBA;
                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineLayout          = m_Layout;
                    desc.shaders                 = shaders.data();
                    desc.shaderNum               = uint32_t(shaders.size());
                    desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode  = VriCullMode_None;
                    desc.rasterization.lineWidth = 1;
                    desc.multisample.sampleNum   = 1;
                    desc.outputMerger.colors     = &color;
                    desc.outputMerger.colorNum   = 1;
                    VriPipeline* result          = nullptr;
                    check(m_Device.core.CreateGraphicsPipeline(m_Device.handle, &desc, &result),
                          "Create blit pipeline");
                    return result;
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

    TextureBlit::~TextureBlit()
    {
        release();
    }

    void TextureBlit::release()
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

    void TextureBlit::setSource(uint32_t slot, Texture& source)
    {
        const VriDescriptor*         descriptors[2] {source.view(), m_Sampler};
        VriDescriptorRangeUpdateDesc updates[2] {};
        for (uint32_t i = 0; i < 2; ++i)
        {
            updates[i].descriptors   = &descriptors[i];
            updates[i].descriptorNum = 1;
        }
        m_Device.core.UpdateDescriptorRanges(m_Sets.at(slot), 0, 2, updates);
        m_Sources.at(slot) = &source;
    }

    void TextureBlit::draw(VriCommandBuffer* cmd, Texture& target, VriRect rectangle, uint32_t slot, bool encodeSrgb)
    {
        auto* source = m_Sources.at(slot);
        if (!source || source == &target)
        {
            throw std::invalid_argument("Blit requires a distinct source texture");
        }
        source->transition(cmd,
                           {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
        target.transition(cmd,
                          {VriAccess_ColorAttachmentRead | VriAccess_ColorAttachmentWrite,
                           VriLayout_ColorAttachment,
                           VriPipelineStage_ColorAttachmentOutput});
        beginColorPass(m_Device, cmd, target.view(), {target.desc.width, target.desc.height});
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
        m_Device.core.CmdSetDescriptorSet(cmd, 0, m_Sets.at(slot));
        const uint32_t parameters[4] {uint32_t(encodeSrgb), 0, 0, 0};
        m_Device.core.CmdSetConstants(cmd, 0, parameters, sizeof(parameters));
        const VriDrawDesc draw {3, 1, 0, 0};
        m_Device.core.CmdDraw(cmd, &draw);
        m_Device.core.CmdEndRendering(cmd);
    }
} // namespace vultra
