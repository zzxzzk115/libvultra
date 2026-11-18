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

    // The scene's materials as `import vultra.material;` reads them: each material's textures and samplers and
    // scene.material_data in descriptor set 1, and the primitive's transform index, material number, type and
    // parameter offset as push constants. Every stage that rasterizes the scene with its materials draws through
    // this class.
    class SceneMaterials
    {
    public:
        static constexpr uint32_t kSet = 1;

        // Set 1 of a stage's pipeline layout: seven textures, one sampler for each, then scene.material_data.
        static std::span<const VriDescriptorRangeDesc> ranges();
        // The push constants of a stage's pipeline layout, for vertex and fragment shaders.
        static VriPushConstantDesc constants();

        // Creates the material sets of `scene` for pipelines of `layout`. The scene must outlive this object.
        [[nodiscard]] static std::expected<std::unique_ptr<SceneMaterials>, Error>
        create(Device& device, VriPipelineLayout* layout, GpuScene& scene);
        ~SceneMaterials();
        SceneMaterials(const SceneMaterials&)            = delete;
        SceneMaterials& operator=(const SceneMaterials&) = delete;

        // Replaces `draws` with the scene's draws whose bounds meet the frustum of `viewProjection`.
        void cull(const glm::mat4& viewProjection, std::vector<uint32_t>& draws) const;

        // Draws `draws` (into GpuScene::draws) from the scene's vertex and index buffers; the push constants carry each
        // draw's instance and material. `pipelines` holds one pipeline per variant:
        // bit 0 double-sided, bit 1 mirrored front face. After each pipeline change, `set0` is bound as set 0.
        void draw(VriCommandBuffer*                cmd,
                  Buffer&                          vertices,
                  Buffer&                          indices,
                  std::span<const uint32_t>        draws,
                  std::span<VriPipeline* const, 4> pipelines,
                  VriDescriptorSet*                set0) const;

        // The set of material `material`, for a stage that binds it itself, such as a compute pass.
        VriDescriptorSet* set(uint32_t material) const;

    private:
        SceneMaterials(Device& device, VriPipelineLayout* layout, GpuScene& scene);

        Device&                        m_Device;
        VriPipelineLayout*             m_Layout;
        GpuScene&                      m_Scene;
        VriDescriptorPool*             m_Pool = nullptr;
        VriDescriptor*                 m_Data = nullptr; // scene.material_data, the same in every set.
        std::vector<VriDescriptorSet*> m_Sets;           // One per material.
        std::vector<VriDescriptor*>    m_Samplers;       // The default sampler, then one per scene sampler.
    };
} // namespace vultra
