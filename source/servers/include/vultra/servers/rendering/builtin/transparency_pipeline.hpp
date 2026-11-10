#pragma once

#include <vultra/servers/rendering/builtin/visibility_raster_pipeline.hpp>

namespace vultra
{
    // The borrowed raster layout and material descriptors use the same ABI as VisibilityRasterPipeline.
    // Transparent fragments accept equal depth and preserve opaque depth.
    // Sampled-depth programs declare transparentOpaqueDepth at set 0, binding 9.
    // Their frame set must be dedicated to this pass; record updates that binding.
    class TransparencyPipeline
    {
    public:
        // Optional programs have the same ABI and assume an exactly zero coat weight.
        // The scene-light program additionally requires usesOnlySceneLights() for the uploaded frame.
        // Selection uses each draw's submitted push constants, including subsequent material edits.
        static std::expected<std::unique_ptr<TransparencyPipeline>, std::string>
        create(Device&              device,
               VriPipelineLayout*   layout,
               const ShaderProgram& program,
               const ShaderProgram* uncoatedProgram    = nullptr,
               const ShaderProgram* sceneLightsProgram = nullptr);

        std::expected<void, std::string> record(VriCommandBuffer*                     commands,
                                                Texture&                              hdr,
                                                Texture&                              depth,
                                                Buffer&                               vertices,
                                                Buffer&                               indices,
                                                VriDescriptorSet*                     frameSet,
                                                std::span<VriDescriptorSet* const>    materialSets,
                                                std::span<const VisibilityRasterDraw> sortedDraws,
                                                bool                                  sceneLightsOnly = false) const;

        bool usesSampledDepth() const noexcept
        {
            return m_UsesSampledDepth;
        }

    private:
        TransparencyPipeline(Device& device, VriPipelineLayout* layout) :
            m_Device(device),
            m_Layout(layout)
        {
        }

        Device&            m_Device;
        VriPipelineLayout* m_Layout;
        bool               m_UsesSampledDepth = false;
        // Eight sidedness/winding/HDR variants for each complete, uncoated and scene-light program.
        std::array<std::unique_ptr<ShaderPipeline>, 24> m_Pipelines;
    };
} // namespace vultra
