#pragma once

#include <vultra/servers/rendering/builtin/visibility_raster_pipeline.hpp>

namespace vultra
{
    inline constexpr std::array<VriFormat, 7> kGBufferFormats {VriFormat_RGBA32_SFLOAT,
                                                               VriFormat_RGBA16_SFLOAT,
                                                               VriFormat_RGBA16_SFLOAT,
                                                               VriFormat_RGBA16_SFLOAT,
                                                               VriFormat_RGBA16_SFLOAT,
                                                               VriFormat_RGBA16_SFLOAT,
                                                               VriFormat_RGBA16_SFLOAT};

    // Non-default encodings require matching geometry and lighting shader variants. Camera-relative half
    // precision limits range and accuracy; depth reconstruction depends on projection and depth precision.
    enum class GBufferPositionEncoding
    {
        eWorld32,
        eCameraRelative16,
        eDepth32,
    };

    inline constexpr std::array<VriFormat, 7> gbufferFormats(GBufferPositionEncoding encoding)
    {
        auto formats = kGBufferFormats;
        if (encoding == GBufferPositionEncoding::eCameraRelative16)
            formats[0] = VriFormat_RGBA16_SFLOAT;
        else if (encoding == GBufferPositionEncoding::eDepth32)
            formats[0] = VriFormat_RG32_SFLOAT;
        return formats;
    }

    // Indexed geometry matching the built-in deferred renderer's seven G-buffer targets.
    class GBufferPipeline
    {
    public:
        static std::expected<std::unique_ptr<GBufferPipeline>, std::string>
        create(Device&                 device,
               VriPipelineLayout*      layout,
               const ShaderProgram&    program,
               bool                    singlePass       = false,
               GBufferPositionEncoding positionEncoding = GBufferPositionEncoding::eWorld32);
        GBufferPipeline(const GBufferPipeline&)            = delete;
        GBufferPipeline& operator=(const GBufferPipeline&) = delete;

        bool belongsTo(const Device& device) const
        {
            return &m_Device == &device;
        }

        VriPipelineLayout* layout() const
        {
            return m_Layout;
        }

        VriPipeline* handle(uint32_t stage, bool doubleSided, bool mirrored) const;

        bool singlePass() const
        {
            return m_SinglePass;
        }

        GBufferPositionEncoding positionEncoding() const
        {
            return m_PositionEncoding;
        }

    private:
        GBufferPipeline(Device& device, VriPipelineLayout* layout, bool singlePass, GBufferPositionEncoding encoding) :
            m_Device(device),
            m_Layout(layout),
            m_SinglePass(singlePass),
            m_PositionEncoding(encoding)
        {
        }

        Device&                                         m_Device;
        VriPipelineLayout*                              m_Layout;
        bool                                            m_SinglePass;
        GBufferPositionEncoding                         m_PositionEncoding;
        std::array<std::unique_ptr<ShaderPipeline>, 12> m_Pipelines;
    };

    // Stage zero clears and writes targets 0..3 and depth. Stage one clears targets 4..6 and
    // tests the preserved depth for equality. Stage two writes all seven targets and depth in one pass.
    // Only the active stage's target pointers are used.
    // Attachments must already be in attachment states; they and descriptors must outlive GPU execution.
    std::expected<void, std::string> recordGBufferGeometry(Device&                               device,
                                                           const GBufferPipeline&                pipeline,
                                                           VriCommandBuffer*                     commands,
                                                           uint32_t                              stage,
                                                           const std::array<Texture*, 7>&        targets,
                                                           Texture&                              depth,
                                                           Buffer&                               vertices,
                                                           Buffer&                               indices,
                                                           VriDescriptorSet*                     frameSet,
                                                           std::span<VriDescriptorSet* const>    materialSets,
                                                           std::span<const VisibilityRasterDraw> draws);
} // namespace vultra
