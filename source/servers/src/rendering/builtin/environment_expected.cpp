#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/environment.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <algorithm>
#include <array>

namespace vultra
{
    namespace
    {
        struct EnvironmentDescriptors
        {
            explicit EnvironmentDescriptors(Device& device) :
                device(device)
            {
            }

            ~EnvironmentDescriptors()
            {
                if (pool)
                    device.core.DestroyDescriptorPool(pool);
                if (sampler)
                    device.core.DestroyDescriptor(sampler);
                if (layout)
                    device.core.DestroyPipelineLayout(layout);
            }

            Device&            device;
            VriPipelineLayout* layout  = nullptr;
            VriDescriptor*     sampler = nullptr;
            VriDescriptorPool* pool    = nullptr;
        };
    } // namespace

    std::expected<std::unique_ptr<Environment>, std::string>
    Environment::create(Device& device, const TextureData& radianceData, const ShaderProgram* program)
    {
        auto result   = std::unique_ptr<Environment>(new Environment(device, Uninitialized {}));
        auto radiance = uploadTextureAssetExpected(device, radianceData);
        if (!radiance)
            return std::unexpected("Upload environment radiance: " + radiance.error());
        result->radiance = std::move(*radiance);
        if (!program)
            return result;
        if (!(device.core.GetFormatSupport(device.handle, result->radiance->desc.format) &
              VriFormatSupport_LinearFilter))
        {
            return std::unexpected("Environment preprocessing requires radiance with native linear filtering support");
        }
        auto diffuse = Texture::create(device, colorTexture({64, 32}, VriFormat_RGBA16_SFLOAT));
        if (!diffuse)
            return std::unexpected(diffuse.error());
        auto specularDesc   = colorTexture({256, 128}, VriFormat_RGBA16_SFLOAT);
        specularDesc.mipNum = 9;
        auto specular       = Texture::create(device, specularDesc);
        if (!specular)
            return std::unexpected(specular.error());
        auto brdf = Texture::create(device, colorTexture({128, 128}, VriFormat_RGBA16_SFLOAT));
        if (!brdf)
            return std::unexpected(brdf.error());
        result->diffuse  = std::move(*diffuse);
        result->specular = std::move(*specular);
        result->brdfLut  = std::move(*brdf);

        EnvironmentDescriptors                      resources(device);
        const std::array<VriDescriptorRangeDesc, 2> ranges {
            {{0, 1, VriDescriptorType_Texture, VriShaderStage_Fragment},
             {1, 1, VriDescriptorType_Sampler, VriShaderStage_Fragment}}};
        const VriDescriptorSetDesc  setDesc {0, ranges.data(), uint32_t(ranges.size())};
        const VriPushConstantDesc   push {0, 16, VriShaderStage_Fragment};
        const VriPipelineLayoutDesc layoutDesc {&setDesc, 1, &push, 1, VriShaderStage_Vertex | VriShaderStage_Fragment};
        auto status = device.core.CreatePipelineLayout(device.handle, &layoutDesc, &resources.layout);
        if (status != VriResult_Success || !resources.layout)
            return std::unexpected("Create environment pipeline layout failed: " + std::to_string(status));
        VriSamplerDesc samplerDesc {};
        samplerDesc.minFilter    = VriFilter_Linear;
        samplerDesc.magFilter    = VriFilter_Linear;
        samplerDesc.addressModeU = VriAddressMode_Repeat;
        samplerDesc.addressModeV = VriAddressMode_ClampToEdge;
        samplerDesc.addressModeW = VriAddressMode_ClampToEdge;
        status                   = device.core.CreateSampler(device.handle, &samplerDesc, &resources.sampler);
        if (status != VriResult_Success || !resources.sampler)
            return std::unexpected("Create environment sampler failed: " + std::to_string(status));
        VriDescriptorPoolDesc poolDesc {};
        poolDesc.descriptorSetMaxNum = 1;
        poolDesc.textureMaxNum       = 1;
        poolDesc.samplerMaxNum       = 1;
        status                       = device.core.CreateDescriptorPool(device.handle, &poolDesc, &resources.pool);
        if (status != VriResult_Success || !resources.pool)
            return std::unexpected("Create environment descriptor pool failed: " + std::to_string(status));
        VriDescriptorSet* set = nullptr;
        status                = device.core.AllocateDescriptorSets(resources.pool, resources.layout, 0, &set, 1);
        if (status != VriResult_Success || !set)
            return std::unexpected("Allocate environment descriptor set failed: " + std::to_string(status));
        auto view = result->radiance->viewExpected();
        if (!view)
            return std::unexpected(view.error());
        const std::array<const VriDescriptor*, 2>         descriptors {*view, resources.sampler};
        const std::array<VriDescriptorRangeUpdateDesc, 2> updates {{{&descriptors[0], 1}, {&descriptors[1], 1}}};
        device.core.UpdateDescriptorRanges(set, 0, uint32_t(updates.size()), updates.data());
        const auto generate = [&](Texture& target, const char* entry) -> std::expected<void, std::string>
        {
            auto pipeline = ShaderPipeline::create(
                device,
                *program,
                {{"screenVertex", VriShaderStage_Vertex}, {entry, VriShaderStage_Fragment}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    VriColorAttachmentDesc color {};
                    color.format         = VriFormat_RGBA16_SFLOAT;
                    color.colorWriteMask = VriColorWrite_RGBA;
                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineCache           = device.pipelineCache;
                    desc.pipelineLayout          = resources.layout;
                    desc.shaders                 = shaders.data();
                    desc.shaderNum               = uint32_t(shaders.size());
                    desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode  = VriCullMode_None;
                    desc.rasterization.lineWidth = 1;
                    desc.multisample.sampleNum   = 1;
                    desc.outputMerger.colors     = &color;
                    desc.outputMerger.colorNum   = 1;
                    VriPipeline* handle          = nullptr;
                    const auto   created         = device.core.CreateGraphicsPipeline(device.handle, &desc, &handle);
                    if (created != VriResult_Success || !handle)
                    {
                        if (handle)
                            device.core.DestroyPipeline(handle);
                        return std::unexpected("Create environment pipeline failed: " + std::to_string(created));
                    }
                    return handle;
                });
            if (!pipeline)
                return std::unexpected(pipeline.error());
            auto frame = Frame::create(device);
            if (!frame)
                return std::unexpected(frame.error());
            auto command = (*frame)->beginExpected();
            if (!command)
                return std::unexpected(command.error());
            target.transition(
                *command,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            for (uint32_t mip = 0; mip < target.desc.mipNum; ++mip)
            {
                auto mipView = target.mipViewExpected(mip);
                if (!mipView)
                    return std::unexpected(mipView.error());
                const Extent size {std::max(target.desc.width >> mip, 1u), std::max(target.desc.height >> mip, 1u)};
                const float  clear[] {0, 0, 0, 1};
                beginColorPass(device, *command, *mipView, size, clear);
                device.core.CmdSetPipelineLayout(*command, resources.layout);
                device.core.CmdSetPipeline(*command, (*pipeline)->handle());
                device.core.CmdSetDescriptorSet(*command, 0, set);
                struct Parameters
                {
                    float    roughness;
                    uint32_t samples;
                    float    padding[2];
                };
                const Parameters parameters {float(mip) / std::max(target.desc.mipNum - 1, 1u), 256, {0, 0}};
                device.core.CmdSetConstants(*command, 0, &parameters, sizeof(parameters));
                const VriDrawDesc draw {3, 1, 0, 0};
                device.core.CmdDraw(*command, &draw);
                device.core.CmdEndRendering(*command);
            }
            target.transition(
                *command,
                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
            return (*frame)->submitAndWaitExpected();
        };
        if (auto generated = generate(*result->diffuse, "diffuseMain"); !generated)
            return std::unexpected(generated.error());
        if (auto generated = generate(*result->specular, "specularMain"); !generated)
            return std::unexpected(generated.error());
        if (auto generated = generate(*result->brdfLut, "brdfMain"); !generated)
            return std::unexpected(generated.error());
        return result;
    }
} // namespace vultra
