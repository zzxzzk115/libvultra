#pragma once
#include <vultra/core/image/image.hpp>
#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <optional>

namespace vultra
{
    // A filtered fullscreen draw into a rectangle. Sources remain owned by the caller.
    class TextureBlit
    {
    public:
        TextureBlit(Device&              device,
                    VriFormat            targetFormat,
                    uint32_t             sourceCount = 1,
                    const ShaderProgram* program     = nullptr);
        static std::expected<std::unique_ptr<TextureBlit>, std::string>
        create(Device& device, VriFormat targetFormat, uint32_t sourceCount, const ShaderProgram& program);
        ~TextureBlit();
        TextureBlit(const TextureBlit&)            = delete;
        TextureBlit& operator=(const TextureBlit&) = delete;

        // Update descriptors before recording draws; one slot per source used in that submission.
        // The legacy setter requires native linear filtering. New callers use the expected interface below.
        void setSource(uint32_t slot, Texture& source);
        // Float-sampled single-layer 2D sources use native filtering when available, otherwise level-zero
        // bilinear loads. A failed bind preserves the previous source, descriptors and filter selection.
        std::expected<void, std::string> setSourceExpected(uint32_t slot, Texture& source);
        // Set encodeSrgb only when writing linear samples to a display UNORM target.
        // Begins and ends its own rendering scope; call outside an active rendering pass.
        void                             draw(VriCommandBuffer*        cmd,
                                              Texture&                 target,
                                              VriRect                  rectangle,
                                              uint32_t                 slot       = 0,
                                              bool                     encodeSrgb = false,
                                              std::optional<ImageView> view       = std::nullopt);
        std::expected<void, std::string> drawExpected(VriCommandBuffer*        cmd,
                                                      Texture&                 target,
                                                      VriRect                  rectangle,
                                                      uint32_t                 slot       = 0,
                                                      bool                     encodeSrgb = false,
                                                      std::optional<ImageView> view       = std::nullopt);

    private:
        struct Uninitialized
        {};

        TextureBlit(Device& device, VriFormat targetFormat, uint32_t sourceCount, Uninitialized);
        void                            release();
        Device&                         m_Device;
        VriFormat                       m_TargetFormat;
        VriPipelineLayout*              m_Layout         = nullptr;
        VriDescriptorPool*              m_Pool           = nullptr;
        VriDescriptor*                  m_Sampler        = nullptr;
        VriDescriptor*                  m_NearestSampler = nullptr;
        std::vector<VriDescriptorSet*>  m_Sets;
        std::vector<Texture*>           m_Sources;
        std::vector<bool>               m_ManualFiltering;
        std::unique_ptr<ShaderPipeline> m_Pipeline;
    };
} // namespace vultra
