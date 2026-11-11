#pragma once
#include <vultra/core/base/error.hpp>
#include <vultra/core/image/image.hpp>
#include <vultra/gpu/resources.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <expected>
#include <memory>
#include <optional>

namespace vultra
{
    // A filtered fullscreen draw into a rectangle. Sources remain owned by the caller.
    class TextureBlit
    {
    public:
        // The pipeline builder keeps this object's address, so it lives behind a unique_ptr.
        [[nodiscard]] static std::expected<std::unique_ptr<TextureBlit>, Error>
        create(Device&              device,
               VriFormat            targetFormat,
               uint32_t             sourceCount = 1,
               const ShaderProgram* program     = nullptr);
        ~TextureBlit();
        TextureBlit(const TextureBlit&)            = delete;
        TextureBlit& operator=(const TextureBlit&) = delete;

        // Update descriptors before recording draws; one slot per source used in that submission.
        [[nodiscard]] std::expected<void, Error> setSource(uint32_t slot, Texture& source);
        // Set encodeSrgb only when writing linear samples to a display UNORM target.
        // Begins and ends its own rendering scope; call outside an active rendering pass.
        [[nodiscard]] std::expected<void, Error> draw(VriCommandBuffer*        cmd,
                                                      Texture&                 target,
                                                      VriRect                  rectangle,
                                                      uint32_t                 slot       = 0,
                                                      bool                     encodeSrgb = false,
                                                      std::optional<ImageView> view       = std::nullopt);

    private:
        TextureBlit(Device& device, uint32_t sourceCount);

        Device&                         m_Device;
        VriPipelineLayout*              m_Layout  = nullptr;
        VriDescriptorPool*              m_Pool    = nullptr;
        VriDescriptor*                  m_Sampler = nullptr;
        std::vector<VriDescriptorSet*>  m_Sets;
        std::vector<Texture*>           m_Sources;
        std::unique_ptr<ShaderPipeline> m_Pipeline;
    };
} // namespace vultra
