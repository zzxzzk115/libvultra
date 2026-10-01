#pragma once
#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

namespace vultra
{
    // A filtered fullscreen draw into a rectangle. Sources remain owned by the caller.
    class TextureBlit
    {
    public:
        TextureBlit(Device& device, VriFormat targetFormat, uint32_t sourceCount = 1);
        ~TextureBlit();
        TextureBlit(const TextureBlit&)            = delete;
        TextureBlit& operator=(const TextureBlit&) = delete;

        // Update descriptors before recording draws; one slot per source used in that submission.
        void setSource(uint32_t slot, Texture& source);
        // Set encodeSrgb only when writing linear samples to a display UNORM target.
        void
        draw(VriCommandBuffer* cmd, Texture& target, VriRect rectangle, uint32_t slot = 0, bool encodeSrgb = false);

    private:
        void                            release();
        Device&                         m_Device;
        VriPipelineLayout*              m_Layout  = nullptr;
        VriDescriptorPool*              m_Pool    = nullptr;
        VriDescriptor*                  m_Sampler = nullptr;
        std::vector<VriDescriptorSet*>  m_Sets;
        std::vector<Texture*>           m_Sources;
        std::unique_ptr<ShaderPipeline> m_Pipeline;
    };
} // namespace vultra
