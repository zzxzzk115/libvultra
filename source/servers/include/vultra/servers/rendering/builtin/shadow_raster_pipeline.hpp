#pragma once

#include <vultra/servers/rendering/builtin/visibility_raster_pipeline.hpp>

namespace vultra
{
    // Directional shadow depth pipelines use the built-in frame/material layout and indexed draws.
    // The borrowed layout must outlive the pipeline.
    class ShadowRasterPipeline
    {
    public:
        static std::expected<std::unique_ptr<ShadowRasterPipeline>, std::string>
        create(Device& device, VriPipelineLayout* layout, const ShaderProgram& program);

        VriPipeline* handle(bool doubleSided, bool mirrored, bool alphaMasked) const;

        VriPipelineLayout* layout() const
        {
            return m_Layout;
        }

        bool belongsTo(const Device& device) const
        {
            return &m_Device == &device;
        }

    private:
        ShadowRasterPipeline(Device& device, VriPipelineLayout* layout) :
            m_Device(device),
            m_Layout(layout)
        {
        }

        Device&                                        m_Device;
        VriPipelineLayout*                             m_Layout;
        std::array<std::unique_ptr<ShaderPipeline>, 8> m_Pipelines;
    };

    // Clears and records one depth cascade. Callers declare graph dependencies for the frame,
    // transforms, material textures, geometry and depth target, and retain resources until completion.
    // Blended surfaces must be filtered according to the caller's shadow policy.
    std::expected<void, std::string> recordShadowRaster(Device&                               device,
                                                        const ShadowRasterPipeline&           pipeline,
                                                        VriCommandBuffer*                     commands,
                                                        uint32_t                              cascade,
                                                        Texture&                              depth,
                                                        Buffer&                               vertices,
                                                        Buffer&                               indices,
                                                        VriDescriptorSet*                     frameSet,
                                                        std::span<VriDescriptorSet* const>    materialSets,
                                                        std::span<const VisibilityRasterDraw> draws);
} // namespace vultra
