#include <vultra/assets/source_file.hpp>
#include <vultra/render/environment.hpp>
#include <vultra/render/texture_upload.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <glm/glm.hpp>

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
        std::expected<TextureLevel, Error> environmentPixels(const std::filesystem::path& path,
                                                             const AssetSource*           source)
        {
            TextureLevel result;
            if (!path.empty())
            {
                int        width      = 0;
                int        height     = 0;
                int        components = 0;
                const auto bytes      = readSourceFile(path, {}, source);
                if (!bytes)
                {
                    return std::unexpected(bytes.error());
                }
                if (bytes->size() > size_t(std::numeric_limits<int>::max()))
                {
                    return makeError("HDR environment exceeds decoder size limit: {}", path.string());
                }
                const std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
                    stbi_loadf_from_memory(reinterpret_cast<const stbi_uc*>(bytes->data()),
                                           int(bytes->size()),
                                           &width,
                                           &height,
                                           &components,
                                           4),
                    &stbi_image_free);
                if (!pixels)
                {
                    return makeError("Load HDR environment {}: {}", path.string(), stbi_failure_reason());
                }
                result.size = {uint32_t(width), uint32_t(height)};
                result.bytes.resize(size_t(width) * height * 4 * sizeof(float));
                std::memcpy(result.bytes.data(), pixels.get(), result.bytes.size());
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

    Environment::Environment(Device&                      device,
                             const std::filesystem::path& hdr,
                             const AssetSource*           source,
                             const AssetSource*           shaders) :
        m_Device(device),
        m_Source(hdr),
        m_AssetSource(source),
        m_ShaderSource(shaders)
    {
    }

    std::expected<std::unique_ptr<Environment>, Error> Environment::create(Device&                      device,
                                                                           const std::filesystem::path& hdr,
                                                                           const AssetSource*           source,
                                                                           const AssetSource*           shaders)
    {
        std::unique_ptr<Environment> environment(new Environment(device, hdr, source, shaders));
        if (auto generated = environment->generate(); !generated)
        {
            return std::unexpected(generated.error());
        }
        return environment;
    }

    std::expected<void, Error> Environment::generate()
    {
        auto& device = m_Device;
        auto  pixels = environmentPixels(m_Source, m_AssetSource);
        if (!pixels)
        {
            return std::unexpected(pixels.error());
        }
        TextureData sourceData;
        sourceData.format = TextureFormat::eRgba32Sfloat;
        sourceData.levels.push_back(std::move(*pixels));
        auto uploaded = uploadTextureAsset(device, sourceData);
        if (!uploaded)
        {
            return std::unexpected(uploaded.error());
        }
        radiance             = std::move(*uploaded);
        const auto createHdr = [&](const VriTextureDesc& desc) -> std::expected<std::unique_ptr<Texture>, Error>
        {
            auto texture = Texture::create(device, desc);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            return std::make_unique<Texture>(std::move(*texture));
        };
        auto specularDesc   = colorTexture({256, 128}, VriFormat_RGBA16_SFLOAT);
        specularDesc.mipNum = 9;
        auto diffuseTexture = createHdr(colorTexture({64, 32}, VriFormat_RGBA16_SFLOAT));
        if (!diffuseTexture)
        {
            return std::unexpected(diffuseTexture.error());
        }
        auto specularTexture = createHdr(specularDesc);
        if (!specularTexture)
        {
            return std::unexpected(specularTexture.error());
        }
        auto lutTexture = createHdr(colorTexture({128, 128}, VriFormat_RGBA16_SFLOAT));
        if (!lutTexture)
        {
            return std::unexpected(lutTexture.error());
        }
        diffuse  = std::move(*diffuseTexture);
        specular = std::move(*specularTexture);
        brdfLut  = std::move(*lutTexture);

        // Preprocessing objects live only for this call; the deleters release them on every return path.
        auto destroyLayout = [&device](VriPipelineLayout* layout)
        {
            device.core.DestroyPipelineLayout(layout);
        };
        auto destroyDescriptor = [&device](VriDescriptor* descriptor)
        {
            device.core.DestroyDescriptor(descriptor);
        };
        auto destroyPool = [&device](VriDescriptorPool* pool)
        {
            device.core.DestroyDescriptorPool(pool);
        };
        VriDescriptorRangeDesc ranges[2] {};
        ranges[0] = {0, 1, VriDescriptorType_Texture, VriShaderStage_Fragment};
        ranges[1] = {1, 1, VriDescriptorType_Sampler, VriShaderStage_Fragment};
        VriDescriptorSetDesc setDesc {};
        setDesc.ranges   = ranges;
        setDesc.rangeNum = 2;
        VriPushConstantDesc   push {0, 16, VriShaderStage_Fragment};
        VriPipelineLayoutDesc layoutDesc {};
        layoutDesc.descriptorSets       = &setDesc;
        layoutDesc.descriptorSetNum     = 1;
        layoutDesc.pushConstants        = &push;
        layoutDesc.pushConstantNum      = 1;
        layoutDesc.shaderStages         = VriShaderStage_Vertex | VriShaderStage_Fragment;
        VriPipelineLayout* layoutHandle = nullptr;
        const auto         layoutCreated =
            check(device.core.CreatePipelineLayout(device.handle, &layoutDesc, &layoutHandle), "Create IBL layout");
        if (!layoutCreated)
        {
            return layoutCreated;
        }
        const std::unique_ptr<VriPipelineLayout, decltype(destroyLayout)> layout(layoutHandle, destroyLayout);
        VriSamplerDesc                                                    samplerDesc {};
        samplerDesc.minFilter        = VriFilter_Linear;
        samplerDesc.magFilter        = VriFilter_Linear;
        samplerDesc.addressModeU     = VriAddressMode_Repeat;
        samplerDesc.addressModeV     = VriAddressMode_ClampToEdge;
        samplerDesc.addressModeW     = VriAddressMode_ClampToEdge;
        VriDescriptor* samplerHandle = nullptr;
        const auto     samplerCreated =
            check(device.core.CreateSampler(device.handle, &samplerDesc, &samplerHandle), "Create environment sampler");
        if (!samplerCreated)
        {
            return samplerCreated;
        }
        const std::unique_ptr<VriDescriptor, decltype(destroyDescriptor)> sampler(samplerHandle, destroyDescriptor);
        VriDescriptorPoolDesc                                             poolDesc {};
        poolDesc.descriptorSetMaxNum  = 1;
        poolDesc.textureMaxNum        = 1;
        poolDesc.samplerMaxNum        = 1;
        VriDescriptorPool* poolHandle = nullptr;
        const auto         poolCreated =
            check(device.core.CreateDescriptorPool(device.handle, &poolDesc, &poolHandle), "Create IBL descriptors");
        if (!poolCreated)
        {
            return poolCreated;
        }
        const std::unique_ptr<VriDescriptorPool, decltype(destroyPool)> pool(poolHandle, destroyPool);
        VriDescriptorSet*                                               set = nullptr;
        if (auto allocated = check(device.core.AllocateDescriptorSets(pool.get(), layout.get(), 0, &set, 1),
                                   "Allocate IBL descriptors");
            !allocated)
        {
            return allocated;
        }
        const auto radianceView = radiance->view();
        if (!radianceView)
        {
            return std::unexpected(radianceView.error());
        }
        const VriDescriptor*         descriptors[2] {*radianceView, sampler.get()};
        VriDescriptorRangeUpdateDesc updates[2] {};
        for (uint32_t i = 0; i < 2; ++i)
        {
            updates[i].descriptors   = &descriptors[i];
            updates[i].descriptorNum = 1;
        }
        device.core.UpdateDescriptorRanges(set, 0, 2, updates);
        const auto prefilter = [&](Texture& texture, const char* entry) -> std::expected<void, Error>
        {
            auto pipeline = ShaderPipeline::create(
                device,
                "builtin/shaders/passes/environment.slang",
                {{"screenVertex", VriShaderStage_Vertex}, {entry, VriShaderStage_Fragment}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, Error>
                {
                    VriColorAttachmentDesc color {};
                    color.format         = VriFormat_RGBA16_SFLOAT;
                    color.colorWriteMask = VriColorWrite_RGBA;
                    VriGraphicsPipelineDesc graphics {};
                    graphics.pipelineCache           = device.pipelineCache;
                    graphics.pipelineLayout          = layout.get();
                    graphics.shaders                 = shaders.data();
                    graphics.shaderNum               = uint32_t(shaders.size());
                    graphics.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                    graphics.rasterization.cullMode  = VriCullMode_None;
                    graphics.rasterization.lineWidth = 1;
                    graphics.multisample.sampleNum   = 1;
                    graphics.outputMerger.colors     = &color;
                    graphics.outputMerger.colorNum   = 1;
                    VriPipeline* result              = nullptr;
                    if (auto created = check(device.core.CreateGraphicsPipeline(device.handle, &graphics, &result),
                                             "Create IBL pipeline");
                        !created)
                    {
                        return std::unexpected(created.error());
                    }
                    return result;
                },
                "builtin/shaders",
                {"builtin/shaders"},
                m_ShaderSource);
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            auto frame = Frame::create(device);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            const auto begun = (*frame)->begin();
            if (!begun)
            {
                return std::unexpected(begun.error());
            }
            auto* cmd = *begun;
            texture.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            for (uint32_t mip = 0; mip < texture.desc.mipNum; ++mip)
            {
                const auto view = texture.view({.mip = mip});
                if (!view)
                {
                    return std::unexpected(view.error());
                }
                const float  clear[4] {0, 0, 0, 1};
                const Extent size {std::max(texture.desc.width >> mip, 1u), std::max(texture.desc.height >> mip, 1u)};
                beginColorPass(device, cmd, *view, size, clear);
                device.core.CmdSetPipelineLayout(cmd, layout.get());
                device.core.CmdSetPipeline(cmd, (*pipeline)->handle());
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
            return (*frame)->submitAndWait();
        };
        if (auto result = prefilter(*diffuse, "diffuseMain"); !result)
        {
            return result;
        }
        if (auto result = prefilter(*specular, "specularMain"); !result)
        {
            return result;
        }
        return prefilter(*brdfLut, "brdfMain");
    }

    std::expected<void, Error> Environment::setSource(const std::filesystem::path& hdr)
    {
        if (hdr == m_Source)
        {
            return {};
        }
        return replace(hdr);
    }

    std::expected<void, Error> Environment::rebuild()
    {
        return replace(m_Source);
    }

    std::expected<void, Error> Environment::replace(const std::filesystem::path& hdr)
    {
        auto replacement = create(m_Device, hdr, m_AssetSource, m_ShaderSource);
        if (!replacement)
        {
            return std::unexpected(replacement.error());
        }
        radiance.swap((*replacement)->radiance);
        diffuse.swap((*replacement)->diffuse);
        specular.swap((*replacement)->specular);
        brdfLut.swap((*replacement)->brdfLut);
        m_Source.swap((*replacement)->m_Source);
        return {};
    }

    const std::filesystem::path& Environment::source() const
    {
        return m_Source;
    }
} // namespace vultra
