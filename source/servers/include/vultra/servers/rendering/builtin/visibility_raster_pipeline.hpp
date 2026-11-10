#pragma once

#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/drivers/rhi/resources.hpp>

#include <glm/glm.hpp>

#include <array>

namespace vultra
{
    class GpuScene;
    class VisibilityColorInput;

    // Indexed visibility raster pipelines. The borrowed layout must outlive this object and match
    // frame_block.slangh, primitive_transform.slangh and gpu_material.slangh.
    class VisibilityRasterPipeline
    {
    public:
        static std::expected<std::unique_ptr<VisibilityRasterPipeline>, std::string>
                     create(Device&                     device,
                            VriPipelineLayout*          layout,
                            const ShaderProgram&        program,
                            const VisibilityColorInput* colorInput = nullptr);
        VriPipeline* handle(bool doubleSided, bool mirrored, bool alphaMasked) const;

        const VisibilityColorInput* colorInput() const
        {
            return m_ColorInput;
        }

        VriPipelineLayout* layout() const
        {
            return m_Layout;
        }
        bool belongsTo(const Device& device) const { return &device == &m_Device; }

    private:
        VisibilityRasterPipeline(Device& device, VriPipelineLayout* layout) : m_Device(device), m_Layout(layout) {}
        Device& m_Device;
        VriPipelineLayout* m_Layout;
        const VisibilityColorInput*                    m_ColorInput = nullptr;
        std::array<std::unique_ptr<ShaderPipeline>, 8> m_Pipelines;
    };

    // Push-constant ABI shared with gpu_material.slangh.
    struct VisibilityRasterMaterial
    {
        glm::vec4 baseColor {1}, surface {}, emission {}, coat {}, specular {}, flags {};
        glm::uvec4 meshlets {}, instance {};
    };

    struct VisibilityRasterDraw
    {
        uint32_t firstIndex = 0, indexCount = 0, materialIndex = 0;
        VisibilityRasterMaterial material;
        bool doubleSided = false, mirrored = false, alphaMasked = false;
    };

    std::expected<std::vector<VisibilityRasterDraw>, std::string>
    visibilityRasterDraws(const GpuScene& scene, std::span<const uint32_t> visiblePrimitives);

    // Clears IDs/depth, records indexed draws and leaves both attachments in attachment states.
    // The caller prepares frame/material descriptor sets and keeps all resources alive until completion.
    // With color input, the paired resolve closes the scope. Use the fused graph pass to record both.
    std::expected<void, std::string> recordVisibilityRaster(
        Device& device, const VisibilityRasterPipeline& pipeline, VriCommandBuffer* commands,
        Texture& ids, Texture& depth, Buffer& vertices, Buffer& indices, VriDescriptorSet* frameSet,
        std::span<VriDescriptorSet* const> materialSets, std::span<const VisibilityRasterDraw> draws);
}
