#include <vultra/assets/source_file.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/builtin/environment.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <utility>

namespace vultra
{
    namespace
    {
        TextureLevel analyticEnvironmentPixels()
        {
            TextureLevel result;
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

        TextureLevel environmentPixels(const std::filesystem::path& path, const AssetSource* source)
        {
            if (path.empty())
                return analyticEnvironmentPixels();
            TextureLevel result;
            int          width      = 0;
            int          height     = 0;
            int          components = 0;
            const auto   bytes      = readSourceFile(path, {}, source);
            if (bytes.size() > size_t(std::numeric_limits<int>::max()))
            {
                throw std::runtime_error("HDR environment exceeds decoder size limit: " + path.string());
            }
            const std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
                stbi_loadf_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                       int(bytes.size()),
                                       &width,
                                       &height,
                                       &components,
                                       4),
                &stbi_image_free);
            if (!pixels)
            {
                throw std::runtime_error("Load HDR environment " + path.string() + ": " + stbi_failure_reason());
            }
            result.size = {uint32_t(width), uint32_t(height)};
            result.bytes.resize(size_t(width) * height * 4 * sizeof(float));
            std::memcpy(result.bytes.data(), pixels.get(), result.bytes.size());
            return result;
        }
    } // namespace

    std::expected<TextureData, std::string> decodeEnvironmentExpected(std::span<const std::byte> bytes,
                                                                      EnvironmentPrecision       precision)
    {
        if (precision != EnvironmentPrecision::eFloat32 && precision != EnvironmentPrecision::ePreferFloat16)
            return std::unexpected("Invalid environment precision");
        const auto encode = [precision](Extent size, const std::byte* pixels) -> std::expected<TextureData, std::string>
        {
            const uint64_t count = uint64_t(size.width) * size.height * 4;
            if (!pixels || size.empty() || count > std::numeric_limits<size_t>::max() / sizeof(float))
                return std::unexpected("HDR environment dimensions exceed supported range");
            bool half = precision == EnvironmentPrecision::ePreferFloat16;
            for (size_t i = 0; i < size_t(count); ++i)
            {
                float value;
                std::memcpy(&value, pixels + i * sizeof(float), sizeof(value));
                if (!std::isfinite(value))
                    return std::unexpected("HDR environment contains non-finite pixels");
                if (std::abs(value) > 65504.f)
                    half = false;
            }
            TextureData data;
            data.format = half ? TextureFormat::eRgba16Sfloat : TextureFormat::eRgba32Sfloat;
            TextureLevel level;
            level.size = size;
            level.bytes.resize(size_t(count) * (half ? sizeof(uint16_t) : sizeof(float)));
            if (half)
            {
                for (size_t i = 0; i < size_t(count); ++i)
                {
                    float value;
                    std::memcpy(&value, pixels + i * sizeof(float), sizeof(value));
                    const uint16_t packed = glm::packHalf1x16(value);
                    std::memcpy(level.bytes.data() + i * sizeof(packed), &packed, sizeof(packed));
                }
            }
            else
                std::memcpy(level.bytes.data(), pixels, level.bytes.size());
            data.levels.push_back(std::move(level));
            return data;
        };
        if (bytes.empty())
        {
            auto analytic = analyticEnvironmentPixels();
            return encode(analytic.size, analytic.bytes.data());
        }
        if (bytes.size() > size_t(std::numeric_limits<int>::max()))
            return std::unexpected("HDR environment exceeds decoder size limit");
        const auto* source = reinterpret_cast<const stbi_uc*>(bytes.data());
        const auto  length = int(bytes.size());
        if (!stbi_is_hdr_from_memory(source, length))
            return std::unexpected("Environment input is not a Radiance HDR image");
        int                                                      width      = 0;
        int                                                      height     = 0;
        int                                                      components = 0;
        const std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
            stbi_loadf_from_memory(source, length, &width, &height, &components, 4),
            &stbi_image_free);
        if (!pixels || width <= 0 || height <= 0)
            return std::unexpected("Cannot decode HDR environment");
        return encode({uint32_t(width), uint32_t(height)}, reinterpret_cast<const std::byte*>(pixels.get()));
    }

    Environment::Environment(Device&                      device,
                             const std::filesystem::path& hdr,
                             const AssetSource*           source,
                             const AssetSource*           shaders) :
        m_Device(device),
        m_Source(hdr),
        m_AssetSource(source),
        m_ShaderSource(shaders)
    {
        TextureData sourceData;
        sourceData.format = TextureFormat::eRgba32Sfloat;
        sourceData.levels.push_back(environmentPixels(hdr, source));
        radiance    = uploadTextureAsset(device, sourceData);
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
                ShaderPipeline pipeline(
                    device,
                    "builtin/shaders/passes/environment.slang",
                    {{"screenVertex", VriShaderStage_Vertex}, {entry, VriShaderStage_Fragment}},
                    [&](std::span<const VriShaderDesc> shaders)
                    {
                        VriColorAttachmentDesc color {};
                        color.format         = VriFormat_RGBA16_SFLOAT;
                        color.colorWriteMask = VriColorWrite_RGBA;
                        VriGraphicsPipelineDesc graphics {};
                        graphics.pipelineCache           = device.pipelineCache;
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
                    {"builtin/shaders"},
                    shaders);
                Frame frame(device);
                auto* cmd = frame.begin();
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

    void Environment::setSource(const std::filesystem::path& hdr)
    {
        if (hdr == m_Source)
        {
            return;
        }
        replace(hdr);
    }

    void Environment::rebuild()
    {
        replace(m_Source);
    }

    void Environment::replace(const std::filesystem::path& hdr)
    {
        Environment replacement(m_Device, hdr, m_AssetSource, m_ShaderSource);
        radiance.swap(replacement.radiance);
        diffuse.swap(replacement.diffuse);
        specular.swap(replacement.specular);
        brdfLut.swap(replacement.brdfLut);
        m_Source.swap(replacement.m_Source);
    }

    const std::filesystem::path& Environment::source() const
    {
        return m_Source;
    }
} // namespace vultra
