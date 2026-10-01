#include "../upload.hpp"

#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/environment.hpp>

#include <glm/glm.hpp>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vultra
{
    namespace
    {
        TextureLevel environmentPixels(const std::filesystem::path& path)
        {
            TextureLevel result;
            if (!path.empty())
            {
                int    width      = 0;
                int    height     = 0;
                int    components = 0;
                float* pixels     = stbi_loadf(path.string().c_str(), &width, &height, &components, 4);
                if (!pixels)
                {
                    throw std::runtime_error("Load HDR environment: " + std::string(stbi_failure_reason()));
                }
                result.size = {uint32_t(width), uint32_t(height)};
                result.bytes.resize(size_t(width) * height * 4 * sizeof(float));
                std::memcpy(result.bytes.data(), pixels, result.bytes.size());
                stbi_image_free(pixels);
                return result;
            }
            result.size = {512, 256};
            std::vector<glm::vec4> pixels(size_t(result.size.width) * result.size.height);
            const auto             sun = glm::normalize(glm::vec3(-0.5f, 0.65f, 0.4f));
            for (uint32_t y = 0; y < result.size.height; ++y)
            {
                for (uint32_t x = 0; x < result.size.width; ++x)
                {
                    const float theta = (float(y) + 0.5f) / result.size.height * std::numbers::pi_v<float>;
                    const float phi   = ((float(x) + 0.5f) / result.size.width - 0.5f) * 2 * std::numbers::pi_v<float>;
                    const glm::vec3 direction {std::cos(phi) * std::sin(theta),
                                               std::cos(theta),
                                               std::sin(phi) * std::sin(theta)};
                    glm::vec3       color = glm::mix(glm::vec3(0.8f, 0.85f, 0.9f),
                                                     glm::vec3(0.12f, 0.3f, 0.7f),
                                                     std::max(direction.y, 0.0f));
                    if (direction.y < 0)
                    {
                        color = glm::vec3(0.08f, 0.07f, 0.055f);
                    }
                    color += glm::vec3(10, 8, 5) * std::pow(std::max(glm::dot(direction, sun), 0.0f), 512.0f);
                    pixels[size_t(y) * result.size.width + x] = glm::vec4(color, 1);
                }
            }
            result.bytes.resize(pixels.size() * sizeof(glm::vec4));
            std::memcpy(result.bytes.data(), pixels.data(), result.bytes.size());
            return result;
        }
    } // namespace

    Environment::Environment(Device& device, const std::filesystem::path& hdr)
    {
        radiance    = uploadTexture(device, TextureFormat::eRgba32Sfloat, 16, {environmentPixels(hdr)});
        diffuse     = std::make_unique<Texture>(device, colorTexture({64, 32}, VriFormat_RGBA16_SFLOAT));
        auto desc   = colorTexture({256, 128}, VriFormat_RGBA16_SFLOAT);
        desc.mipNum = 9;
        specular    = std::make_unique<Texture>(device, desc);
        brdfLut     = std::make_unique<Texture>(device, colorTexture({128, 128}, VriFormat_RGBA16_SFLOAT));
        VriPipelineLayout* layout  = nullptr;
        VriDescriptor*     sampler = nullptr;
        VriDescriptorPool* pool    = nullptr;
        auto               release = [&]
        {
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            if (sampler)
            {
                device.core.DestroyDescriptor(sampler);
            }
            if (layout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        };
        try
        {
            VriDescriptorRangeDesc ranges[2] {};
            ranges[0] = {0, 1, VriDescriptorType_Texture, VriShaderStage_Fragment};
            ranges[1] = {1, 1, VriDescriptorType_Sampler, VriShaderStage_Fragment};
            VriDescriptorSetDesc setDesc {};
            setDesc.ranges   = ranges;
            setDesc.rangeNum = 2;
            VriPushConstantDesc   push {0, 16, VriShaderStage_Fragment};
            VriPipelineLayoutDesc layoutDesc {};
            layoutDesc.descriptorSets   = &setDesc;
            layoutDesc.descriptorSetNum = 1;
            layoutDesc.pushConstants    = &push;
            layoutDesc.pushConstantNum  = 1;
            layoutDesc.shaderStages     = VriShaderStage_Vertex | VriShaderStage_Fragment;
            check(device.core.CreatePipelineLayout(device.handle, &layoutDesc, &layout), "Create IBL layout");
            VriSamplerDesc samplerDesc {};
            samplerDesc.minFilter    = VriFilter_Linear;
            samplerDesc.magFilter    = VriFilter_Linear;
            samplerDesc.addressModeU = VriAddressMode_Repeat;
            samplerDesc.addressModeV = VriAddressMode_ClampToEdge;
            samplerDesc.addressModeW = VriAddressMode_ClampToEdge;
            check(device.core.CreateSampler(device.handle, &samplerDesc, &sampler), "Create environment sampler");
            VriDescriptorPoolDesc poolDesc {};
            poolDesc.descriptorSetMaxNum = 1;
            poolDesc.textureMaxNum       = 1;
            poolDesc.samplerMaxNum       = 1;
            check(device.core.CreateDescriptorPool(device.handle, &poolDesc, &pool), "Create IBL descriptors");
            VriDescriptorSet* set = nullptr;
            check(device.core.AllocateDescriptorSets(pool, layout, 0, &set, 1), "Allocate IBL descriptors");
            const VriDescriptor*         descriptors[2] {radiance->view(), sampler};
            VriDescriptorRangeUpdateDesc updates[2] {};
            for (uint32_t i = 0; i < 2; ++i)
            {
                updates[i].descriptors   = &descriptors[i];
                updates[i].descriptorNum = 1;
            }
            device.core.UpdateDescriptorRanges(set, 0, 2, updates);
            auto generate = [&](Texture& texture, const char* entry)
            {
                ShaderPipeline pipeline(device,
                                        "builtin/shaders/passes/environment.slang",
                                        {{"screenVertex", VriShaderStage_Vertex}, {entry, VriShaderStage_Fragment}},
                                        [&](std::span<const VriShaderDesc> shaders)
                                        {
                                            VriColorAttachmentDesc color {};
                                            color.format         = VriFormat_RGBA16_SFLOAT;
                                            color.colorWriteMask = VriColorWrite_RGBA;
                                            VriGraphicsPipelineDesc graphics {};
                                            graphics.pipelineLayout          = layout;
                                            graphics.shaders                 = shaders.data();
                                            graphics.shaderNum               = uint32_t(shaders.size());
                                            graphics.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                                            graphics.rasterization.cullMode  = VriCullMode_None;
                                            graphics.rasterization.lineWidth = 1;
                                            graphics.multisample.sampleNum   = 1;
                                            graphics.outputMerger.colors     = &color;
                                            graphics.outputMerger.colorNum   = 1;
                                            VriPipeline* result              = nullptr;
                                            check(device.core.CreateGraphicsPipeline(device.handle, &graphics, &result),
                                                  "Create IBL pipeline");
                                            return result;
                                        },
                                        "builtin/shaders",
                                        {"builtin/shaders"});
                Frame          frame(device);
                auto*          cmd = frame.begin();
                texture.transition(cmd,
                                   {VriAccess_ColorAttachmentWrite,
                                    VriLayout_ColorAttachment,
                                    VriPipelineStage_ColorAttachmentOutput});
                for (uint32_t mip = 0; mip < texture.desc.mipNum; ++mip)
                {
                    const float  clear[4] {0, 0, 0, 1};
                    const Extent size {std::max(texture.desc.width >> mip, 1u),
                                       std::max(texture.desc.height >> mip, 1u)};
                    beginColorPass(device, cmd, texture.mipView(mip), size, clear);
                    device.core.CmdSetPipelineLayout(cmd, layout);
                    device.core.CmdSetPipeline(cmd, pipeline.handle());
                    device.core.CmdSetDescriptorSet(cmd, 0, set);
                    struct Parameters
                    {
                        float    roughness;
                        uint32_t samples;
                        float    padding[2];
                    } parameters {float(mip) / std::max(texture.desc.mipNum - 1, 1u), 256, {0, 0}};
                    device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
                    const VriDrawDesc draw {3, 1, 0, 0};
                    device.core.CmdDraw(cmd, &draw);
                    device.core.CmdEndRendering(cmd);
                }
                texture.transition(
                    cmd,
                    {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
                frame.submitAndWait();
            };
            generate(*diffuse, "diffuseMain");
            generate(*specular, "specularMain");
            generate(*brdfLut, "brdfMain");
        }
        catch (...)
        {
            release();
            throw;
        }
        release();
    }
} // namespace vultra
