#include <vultra/render/scene.hpp>
#include <vultra/render/scene_materials.hpp>

#include <array>
#include <cstddef>

namespace vultra
{
    namespace
    {
        // Keep in sync with MaterialDraw in builtin/shaders/resources/gpu_material.slangh: the primitive's transform
        // index, its material number, the material's type and the first word of its parameter block.
        using MaterialDraw = glm::uvec4;

        // Seven textures, one sampler for each, then scene.material_data.
        constexpr uint32_t kRangeCount = 2 * kMaterialTextureCount + 1;

        constexpr std::array<VriDescriptorRangeDesc, kRangeCount> kRanges = []
        {
            std::array<VriDescriptorRangeDesc, kRangeCount> ranges {};
            for (uint32_t i = 0; i < 2 * kMaterialTextureCount; ++i)
            {
                const auto type = i < kMaterialTextureCount ? VriDescriptorType_Texture : VriDescriptorType_Sampler;
                ranges[i]       = {i, 1, type, VriShaderStage_Fragment};
            }
            ranges[2 * kMaterialTextureCount] = {2 * kMaterialTextureCount,
                                                 1,
                                                 VriDescriptorType_StructuredBuffer,
                                                 VriShaderStage_Fragment};
            return ranges;
        }();

        VriSamplerDesc repeatSampler()
        {
            VriSamplerDesc sampler {};
            sampler.minFilter    = VriFilter_Linear;
            sampler.magFilter    = VriFilter_Linear;
            sampler.mipmapMode   = VriMipmapMode_Linear;
            sampler.addressModeU = VriAddressMode_Repeat;
            sampler.addressModeV = VriAddressMode_Repeat;
            sampler.addressModeW = VriAddressMode_Repeat;
            sampler.maxLod       = 32;
            return sampler;
        }

        std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& matrix)
        {
            const glm::vec4 x {matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]};
            const glm::vec4 y {matrix[0][1], matrix[1][1], matrix[2][1], matrix[3][1]};
            const glm::vec4 z {matrix[0][2], matrix[1][2], matrix[2][2], matrix[3][2]};
            const glm::vec4 w {matrix[0][3], matrix[1][3], matrix[2][3], matrix[3][3]};
            std::array      planes {w + x, w - x, w + y, w - y, z, w - z}; // VRI depth is [0,1].
            for (auto& plane : planes)
            {
                const float length = glm::length(glm::vec3(plane));
                if (length > 0)
                {
                    plane /= length;
                }
            }
            return planes;
        }

        bool intersects(const PrimitiveBounds& bounds, const std::array<glm::vec4, 6>& planes)
        {
            for (const auto& plane : planes)
            {
                const glm::vec3 normal(plane);
                const auto      support =
                    glm::dot(normal, bounds.center) + plane.w + glm::dot(glm::abs(normal), bounds.extent);
                // Keep touching boxes despite float transform/clip-plane roundoff.
                const auto margin = 1e-4f * (1 + glm::dot(glm::abs(normal), glm::abs(bounds.center) + bounds.extent));
                if (support < -margin)
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    std::span<const VriDescriptorRangeDesc> SceneMaterials::ranges()
    {
        return kRanges;
    }

    VriDescriptorSet* SceneMaterials::set(uint32_t material) const
    {
        return m_Sets[material];
    }

    VriPushConstantDesc SceneMaterials::constants()
    {
        // Register b2: on D3D12 the frame set's constant buffers take b0 and b1 in the same space.
        return {2, sizeof(MaterialDraw), VriShaderStage_Vertex | VriShaderStage_Fragment};
    }

    SceneMaterials::SceneMaterials(Device& device, VriPipelineLayout* layout, GpuScene& scene) :
        m_Device(device),
        m_Layout(layout),
        m_Scene(scene)
    {
    }

    std::expected<std::unique_ptr<SceneMaterials>, Error>
    SceneMaterials::create(Device& device, VriPipelineLayout* layout, GpuScene& scene)
    {
        // The destructor releases whatever was created when a later step fails.
        std::unique_ptr<SceneMaterials> result(new SceneMaterials(device, layout, scene));
        auto&                           samplers = result->m_Samplers;
        // Sampler 0 is the default; scene sampler i is stored at i + 1.
        samplers.resize(scene.samplers.size() + 1);
        for (size_t i = 0; i < samplers.size(); ++i)
        {
            const auto desc = i == 0 ? repeatSampler() : scene.samplers[i - 1];
            if (auto created =
                    check(device.core.CreateSampler(device.handle, &desc, &samplers[i]), "Create material sampler");
                !created)
            {
                return std::unexpected(created.error());
            }
        }
        const auto count = uint32_t(scene.materials.size());
        if (count == 0)
        {
            return result;
        }
        const VriBufferViewDesc data {scene.materialData->handle,
                                      VriDescriptorType_StructuredBuffer,
                                      VriFormat_Unknown,
                                      0,
                                      scene.materialData->desc.size};
        if (auto created =
                check(device.core.CreateBufferView(device.handle, &data, &result->m_Data), "Create material data view");
            !created)
        {
            return std::unexpected(created.error());
        }
        VriDescriptorPoolDesc pool {};
        pool.descriptorSetMaxNum    = count;
        pool.textureMaxNum          = count * kMaterialTextureCount;
        pool.samplerMaxNum          = count * kMaterialTextureCount;
        pool.structuredBufferMaxNum = count;
        if (auto created =
                check(device.core.CreateDescriptorPool(device.handle, &pool, &result->m_Pool), "Create material pool");
            !created)
        {
            return std::unexpected(created.error());
        }
        result->m_Sets.resize(count);
        if (auto allocated =
                check(device.core.AllocateDescriptorSets(result->m_Pool, layout, kSet, result->m_Sets.data(), count),
                      "Allocate material sets");
            !allocated)
        {
            return std::unexpected(allocated.error());
        }
        for (uint32_t material = 0; material < count; ++material)
        {
            const auto&                  source = scene.materials[material];
            const MaterialTexture        slots[] {source.baseColorTexture,
                                                  source.metallicRoughnessTexture,
                                                  source.normalTexture,
                                                  source.occlusionTexture,
                                                  source.emissionTexture,
                                                  source.specularTexture,
                                                  source.specularColorTexture};
            const VriDescriptor*         textures[kMaterialTextureCount] {};
            const VriDescriptor*         materialSamplers[kMaterialTextureCount] {};
            const VriDescriptor*         data = result->m_Data;
            VriDescriptorRangeUpdateDesc updates[kRangeCount] {};
            for (uint32_t slot = 0; slot < kMaterialTextureCount; ++slot)
            {
                if (slots[slot].sampler < -1 ||
                    (slots[slot].sampler >= 0 && size_t(slots[slot].sampler) >= scene.samplers.size()))
                {
                    return makeError("Invalid material sampler index");
                }
                const auto view = scene.materialTextures[material][slot]->view();
                if (!view)
                {
                    return std::unexpected(view.error());
                }
                textures[slot]                        = *view;
                materialSamplers[slot]                = samplers[size_t(slots[slot].sampler + 1)];
                updates[slot]                         = {&textures[slot], 1};
                updates[kMaterialTextureCount + slot] = {&materialSamplers[slot], 1};
            }
            updates[2 * kMaterialTextureCount] = {&data, 1};
            device.core.UpdateDescriptorRanges(result->m_Sets[material], 0, kRangeCount, updates);
        }
        return result;
    }

    SceneMaterials::~SceneMaterials()
    {
        if (m_Pool)
        {
            m_Device.core.DestroyDescriptorPool(m_Pool);
        }
        if (m_Data)
        {
            m_Device.core.DestroyDescriptor(m_Data);
        }
        for (auto* sampler : m_Samplers)
        {
            if (sampler)
            {
                m_Device.core.DestroyDescriptor(sampler);
            }
        }
    }

    void SceneMaterials::cull(const glm::mat4& viewProjection, std::vector<uint32_t>& draws) const
    {
        draws.clear();
        const auto planes = frustumPlanes(viewProjection);
        for (uint32_t index = 0; index < m_Scene.draws.size(); ++index)
        {
            if (m_Scene.primitives[m_Scene.draws[index].primitive].indexCount &&
                intersects(m_Scene.drawBounds(index), planes))
            {
                draws.push_back(index);
            }
        }
    }

    void SceneMaterials::draw(VriCommandBuffer*                cmd,
                              Buffer&                          vertices,
                              Buffer&                          indices,
                              std::span<const uint32_t>        draws,
                              std::span<VriPipeline* const, 4> pipelines,
                              VriDescriptorSet*                set0) const
    {
        m_Device.core.CmdSetPipelineLayout(cmd, m_Layout);
        const VriVertexBufferBinding binding {vertices.handle, 0};
        m_Device.core.CmdSetVertexBuffers(cmd, 0, &binding, 1);
        m_Device.core.CmdSetIndexBuffer(cmd, indices.handle, 0, VriIndexType_UInt32);
        VriPipeline* activePipeline = nullptr;
        for (const uint32_t index : draws)
        {
            const auto&    draw      = m_Scene.draws[index];
            const auto&    primitive = m_Scene.primitives[draw.primitive];
            const auto&    material  = m_Scene.materials[draw.material];
            const uint32_t variant =
                (material.doubleSided ? 1u : 0u) + (m_Scene.instanceMirrored(draw.instance) ? 2u : 0u);
            auto* pipeline = pipelines[variant];
            if (pipeline != activePipeline)
            {
                m_Device.core.CmdSetPipeline(cmd, pipeline);
                m_Device.core.CmdSetDescriptorSet(cmd, 0, set0);
                activePipeline = pipeline;
            }
            const auto&        header = m_Scene.materialHeader(draw.material);
            const MaterialDraw parameters {draw.instance, draw.material, header.type, header.offset};
            m_Device.core.CmdSetConstants(cmd, 0, &parameters, sizeof(parameters));
            m_Device.core.CmdSetDescriptorSet(cmd, kSet, m_Sets[draw.material]);
            const VriDrawIndexedDesc indexed {primitive.indexCount, 1, primitive.firstIndex, 0, 0};
            m_Device.core.CmdDrawIndexed(cmd, &indexed);
        }
    }
} // namespace vultra
