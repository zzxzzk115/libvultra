#include <vultra/servers/rendering/texture_blit.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace vultra
{
    TextureBlit::TextureBlit(Device& device, VriFormat targetFormat, uint32_t sourceCount, Uninitialized) :
        m_Device(device),
        m_TargetFormat(targetFormat),
        m_Sets(sourceCount),
        m_Sources(sourceCount),
        m_ManualFiltering(sourceCount)
    {
    }

    std::expected<std::unique_ptr<TextureBlit>, std::string>
    TextureBlit::create(Device& device, VriFormat targetFormat, uint32_t sourceCount, const ShaderProgram& program)
    {
        if (!sourceCount)
        {
            return std::unexpected("TextureBlit needs at least one source slot");
        }
        const auto* parameters = program.parameters.field("parameters");
        const auto* element    = parameters ? parameters->field("$element") : nullptr;
        const bool  pushConstant =
            parameters && std::ranges::any_of(parameters->offsets,
                                              [](const auto& offset)
                                              {
                                                  return offset.kind == ShaderOffsetKind::ePushConstant &&
                                                         offset.offset == 0;
                                              });
        if (!pushConstant || !element || element->size != 32)
        {
            return std::unexpected("TextureBlit requires its 32-byte push-constant layout");
        }

        struct Field
        {
            const char*      name;
            ShaderScalarType type;
            uint64_t         offset;
        };

        const std::array fields {Field {"encodeSrgb", ShaderScalarType::eUnsigned, 0},
                                 Field {"channel", ShaderScalarType::eUnsigned, 4},
                                 Field {"minimum", ShaderScalarType::eFloat, 8},
                                 Field {"maximum", ShaderScalarType::eFloat, 12},
                                 Field {"remap", ShaderScalarType::eUnsigned, 16},
                                 Field {"manualFilter", ShaderScalarType::eUnsigned, 20}};
        for (const auto& expected : fields)
        {
            const auto* field = element->field(expected.name);
            if (!field || field->kind != ShaderParameterKind::eScalar || field->scalar != expected.type ||
                field->size != 4 ||
                !std::ranges::any_of(field->offsets,
                                     [&expected](const auto& offset)
                                     {
                                         return offset.kind == ShaderOffsetKind::eUniform &&
                                                offset.offset == expected.offset;
                                     }))
            {
                return std::unexpected(std::string("TextureBlit push-constant field mismatch: ") + expected.name);
            }
        }
        auto result =
            std::unique_ptr<TextureBlit>(new TextureBlit(device, targetFormat, sourceCount, Uninitialized {}));
        VriDescriptorRangeDesc ranges[2] {{0, 1, VriDescriptorType_Texture, VriShaderStage_Fragment},
                                          {1, 1, VriDescriptorType_Sampler, VriShaderStage_Fragment}};
        VriDescriptorSetDesc   set {};
        set.ranges   = ranges;
        set.rangeNum = 2;
        VriPushConstantDesc   push {0, 32, VriShaderStage_Fragment};
        VriPipelineLayoutDesc layout {};
        layout.descriptorSets   = &set;
        layout.descriptorSetNum = 1;
        layout.pushConstants    = &push;
        layout.pushConstantNum  = 1;
        layout.shaderStages     = VriShaderStage_Vertex | VriShaderStage_Fragment;
        auto status             = device.core.CreatePipelineLayout(device.handle, &layout, &result->m_Layout);
        if (status != VriResult_Success || !result->m_Layout)
        {
            return std::unexpected("Create blit layout failed: " + std::to_string(status));
        }
        VriSamplerDesc sampler {};
        sampler.minFilter = sampler.magFilter = VriFilter_Linear;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VriAddressMode_ClampToEdge;
        status = device.core.CreateSampler(device.handle, &sampler, &result->m_Sampler);
        if (status != VriResult_Success || !result->m_Sampler)
        {
            return std::unexpected("Create blit sampler failed: " + std::to_string(status));
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum = pool.textureMaxNum = pool.samplerMaxNum = sourceCount;
        status = device.core.CreateDescriptorPool(device.handle, &pool, &result->m_Pool);
        if (status != VriResult_Success || !result->m_Pool)
        {
            return std::unexpected("Create blit descriptor pool failed: " + std::to_string(status));
        }
        status =
            device.core.AllocateDescriptorSets(result->m_Pool, result->m_Layout, 0, result->m_Sets.data(), sourceCount);
        if (status != VriResult_Success)
        {
            return std::unexpected("Allocate blit descriptor sets failed: " + std::to_string(status));
        }
        for (auto* descriptorSet : result->m_Sets)
        {
            if (!descriptorSet)
            {
                return std::unexpected("Allocate blit descriptor set returned null");
            }
        }
        auto pipeline = ShaderPipeline::create(
            device,
            program,
            {{"screenVertex", VriShaderStage_Vertex}, {"blitMain", VriShaderStage_Fragment}},
            [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                VriColorAttachmentDesc color {};
                color.format         = targetFormat;
                color.colorWriteMask = VriColorWrite_RGBA;
                VriGraphicsPipelineDesc desc {};
                desc.pipelineCache           = device.pipelineCache;
                desc.pipelineLayout          = result->m_Layout;
                desc.shaders                 = shaders.data();
                desc.shaderNum               = uint32_t(shaders.size());
                desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                desc.rasterization.cullMode  = VriCullMode_None;
                desc.rasterization.lineWidth = 1;
                desc.multisample.sampleNum   = 1;
                desc.outputMerger.colors     = &color;
                desc.outputMerger.colorNum   = 1;
                VriPipeline* handle          = nullptr;
                const auto   built           = device.core.CreateGraphicsPipeline(device.handle, &desc, &handle);
                if (built != VriResult_Success || !handle)
                {
                    if (handle)
                    {
                        device.core.DestroyPipeline(handle);
                    }
                    return std::unexpected("Create blit pipeline failed: " + std::to_string(built));
                }
                return handle;
            });
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        result->m_Pipeline = std::move(*pipeline);
        return result;
    }

    TextureBlit::TextureBlit(Device&              device,
                             VriFormat            targetFormat,
                             uint32_t             sourceCount,
                             const ShaderProgram* program) :
        m_Device(device),
        m_TargetFormat(targetFormat),
        m_Sets(sourceCount),
        m_Sources(sourceCount),
        m_ManualFiltering(sourceCount)
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
            VriPushConstantDesc   push {0, 32, VriShaderStage_Fragment};
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
            const std::vector<ShaderEntry> entries {{"screenVertex", VriShaderStage_Vertex},
                                                    {"blitMain", VriShaderStage_Fragment}};
            const auto                     build = [this, targetFormat](std::span<const VriShaderDesc> shaders)
            {
                VriColorAttachmentDesc color {};
                color.format         = targetFormat;
                color.colorWriteMask = VriColorWrite_RGBA;
                VriGraphicsPipelineDesc desc {};
                desc.pipelineCache           = m_Device.pipelineCache;
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
                check(m_Device.core.CreateGraphicsPipeline(m_Device.handle, &desc, &result), "Create blit pipeline");
                return result;
            };
            if (program)
            {
                m_Pipeline = std::make_unique<ShaderPipeline>(device, *program, entries, build);
            }
            else
            {
                m_Pipeline = std::make_unique<ShaderPipeline>(device,
                                                              "builtin/shaders/passes/texture_blit.slang",
                                                              entries,
                                                              build,
                                                              "builtin/shaders",
                                                              std::vector<std::filesystem::path> {"builtin/shaders"});
            }
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
        if (m_NearestSampler)
        {
            m_Device.core.DestroyDescriptor(m_NearestSampler);
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
        m_Sources.at(slot)      = &source;
        m_ManualFiltering[slot] = false;
    }

    std::expected<void, std::string> TextureBlit::setSourceExpected(uint32_t slot, Texture& source)
    {
        if (slot >= m_Sets.size() || !source.belongsTo(m_Device) || source.desc.type != VriTextureType_2D ||
            source.desc.layerNum != 1 || source.desc.sampleNum != 1 ||
            !(source.desc.usage & VriTextureUsage_ShaderResource))
        {
            return std::unexpected("Invalid blit source or slot");
        }
        constexpr std::array integerFormats {VriFormat_R8_UINT,     VriFormat_R8_SINT,     VriFormat_RG8_UINT,
                                             VriFormat_RG8_SINT,    VriFormat_RGBA8_UINT,  VriFormat_RGBA8_SINT,
                                             VriFormat_R16_UINT,    VriFormat_R16_SINT,    VriFormat_RG16_UINT,
                                             VriFormat_RG16_SINT,   VriFormat_RGBA16_UINT, VriFormat_RGBA16_SINT,
                                             VriFormat_R32_UINT,    VriFormat_R32_SINT,    VriFormat_RG32_UINT,
                                             VriFormat_RG32_SINT,   VriFormat_RGB32_UINT,  VriFormat_RGB32_SINT,
                                             VriFormat_RGBA32_UINT, VriFormat_RGBA32_SINT, VriFormat_S8_UINT};
        const auto           support = m_Device.core.GetFormatSupport(m_Device.handle, source.desc.format);
        if (!(support & VriFormatSupport_Texture) ||
            std::ranges::find(integerFormats, source.desc.format) != integerFormats.end())
        {
            return std::unexpected("Blit source must support floating-point texture reads");
        }
        auto view = source.viewExpected();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        const bool manual = !(support & VriFormatSupport_LinearFilter);
        if (manual && !m_NearestSampler)
        {
            VriSamplerDesc sampler {};
            sampler.minFilter = sampler.magFilter = VriFilter_Nearest;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VriAddressMode_ClampToEdge;
            VriDescriptor* nearest                                             = nullptr;
            const auto     status = m_Device.core.CreateSampler(m_Device.handle, &sampler, &nearest);
            if (status != VriResult_Success || !nearest)
            {
                if (nearest)
                {
                    m_Device.core.DestroyDescriptor(nearest);
                }
                return std::unexpected("Create blit nearest sampler failed: " + std::to_string(status));
            }
            m_NearestSampler = nearest;
        }
        // A nearest sampler keeps the unfilterable image legal even under static shader validation.
        const VriDescriptor*         descriptors[2] {*view, manual ? m_NearestSampler : m_Sampler};
        VriDescriptorRangeUpdateDesc updates[2] {};
        for (uint32_t i = 0; i < 2; ++i)
        {
            updates[i].descriptors   = &descriptors[i];
            updates[i].descriptorNum = 1;
        }
        m_Device.core.UpdateDescriptorRanges(m_Sets[slot], 0, 2, updates);
        m_Sources[slot]         = &source;
        m_ManualFiltering[slot] = manual;
        return {};
    }

    void TextureBlit::draw(VriCommandBuffer*        cmd,
                           Texture&                 target,
                           VriRect                  rectangle,
                           uint32_t                 slot,
                           bool                     encodeSrgb,
                           std::optional<ImageView> view)
    {
        if (auto result = drawExpected(cmd, target, rectangle, slot, encodeSrgb, view); !result)
        {
            throw std::invalid_argument(result.error());
        }
    }

    std::expected<void, std::string> TextureBlit::drawExpected(VriCommandBuffer*        cmd,
                                                               Texture&                 target,
                                                               VriRect                  rectangle,
                                                               uint32_t                 slot,
                                                               bool                     encodeSrgb,
                                                               std::optional<ImageView> view)
    {
        if (!cmd || slot >= m_Sources.size() || !target.belongsTo(m_Device) || target.desc.format != m_TargetFormat ||
            !(target.desc.usage & VriTextureUsage_ColorAttachment) || rectangle.x < 0 || rectangle.y < 0 ||
            !rectangle.width || !rectangle.height || uint64_t(rectangle.x) + rectangle.width > target.desc.width ||
            uint64_t(rectangle.y) + rectangle.height > target.desc.height)
        {
            return std::unexpected("Invalid blit target, rectangle or slot");
        }
        if (view && (view->channel > ImageChannel::eLuminance || !std::isfinite(view->minimum) ||
                     !std::isfinite(view->maximum) || view->maximum <= view->minimum ||
                     !std::isfinite(view->maximum - view->minimum)))
        {
            return std::unexpected("Image view requires a valid channel and finite increasing range");
        }
        auto* source = m_Sources[slot];
        if (!source || source == &target)
        {
            return std::unexpected("Blit requires a distinct source texture");
        }
        auto targetView = target.viewExpected();
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
            uint32_t manualFilter;
            uint32_t padding[2];
        };

        static_assert(sizeof(Parameters) == 32);
        const auto       settings = view.value_or(ImageView {});
        const Parameters parameters {uint32_t(encodeSrgb),
                                     uint32_t(settings.channel),
                                     settings.minimum,
                                     settings.maximum,
                                     uint32_t(view.has_value()),
                                     uint32_t(m_ManualFiltering[slot]),
                                     {}};
        m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
        const VriDrawDesc draw {3, 1, 0, 0};
        m_Device.core.CmdDraw(cmd, &draw);
        m_Device.core.CmdEndRendering(cmd);
        return {};
    }
} // namespace vultra
