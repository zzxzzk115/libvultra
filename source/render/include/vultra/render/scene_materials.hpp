#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/gpu/device.hpp>
#include <vultra/gpu/resources.hpp>

#include <glm/mat4x4.hpp>

#include <expected>
#include <memory>
#include <span>
#include <vector>

namespace vultra
{
    class GpuScene;

    // The scene's materials as `import vultra.material;` reads them: each material's textures and samplers in
    // descriptor set 1 and its constants, with the primitive's transform index, as push constants. Every stage
    // that rasterizes the scene with its materials draws through this class.
    class SceneMaterials
    {
    public:
        static constexpr uint32_t kSet = 1;

        // Set 1 of a stage's pipeline layout: seven textures, then one sampler for each.
        static std::span<const VriDescriptorRangeDesc> ranges();
        // The push constants of a stage's pipeline layout, for vertex and fragment shaders.
        static VriPushConstantDesc constants();

        // Creates the material sets of `scene` for pipelines of `layout`. The scene must outlive this object.
        [[nodiscard]] static std::expected<std::unique_ptr<SceneMaterials>, Error>
        create(Device& device, VriPipelineLayout* layout, GpuScene& scene);
        ~SceneMaterials();
        SceneMaterials(const SceneMaterials&)            = delete;
        SceneMaterials& operator=(const SceneMaterials&) = delete;

        // Replaces `primitives` with the drawable primitives whose bounds meet the frustum of `viewProjection`.
        void cull(const glm::mat4& viewProjection, std::vector<uint32_t>& primitives) const;

        // Draws `primitives` from the scene's vertex and index buffers. `pipelines` holds one pipeline per variant:
        // bit 0 double-sided, bit 1 mirrored front face. After each pipeline change, `set0` is bound as set 0.
        void draw(VriCommandBuffer*                cmd,
                  Buffer&                          vertices,
                  Buffer&                          indices,
                  std::span<const uint32_t>        primitives,
                  std::span<VriPipeline* const, 4> pipelines,
                  VriDescriptorSet*                set0) const;

    private:
        SceneMaterials(Device& device, VriPipelineLayout* layout, GpuScene& scene);

        Device&                        m_Device;
        VriPipelineLayout*             m_Layout;
        GpuScene&                      m_Scene;
        VriDescriptorPool*             m_Pool = nullptr;
        std::vector<VriDescriptorSet*> m_Sets;     // One per material.
        std::vector<VriDescriptor*>    m_Samplers; // The default sampler, then one per scene sampler.
    };
} // namespace vultra
