#pragma once
#include <vultra/core/math/extent.hpp>
#include <vultra/gpu/device.hpp>

#include <optional>
#include <vector>

namespace vultra
{
    // One mip level, one array layer, or one of each. A cube face is a layer; a 3D texture has a single layer,
    // so its depth slices share their mip's state. An absent mip or layer means all of them.
    struct TextureRange
    {
        std::optional<uint32_t> mip;
        std::optional<uint32_t> layer;
    };

    // The state of every mip and layer is tracked separately; a transition of the whole texture is one barrier
    // while all of them share a state. Moving transfers ownership of the VRI texture and its views; the
    // moved-from texture is empty.
    class Texture
    {
    public:
        // A borrowed texture is wrapped without creating anything.
        [[nodiscard]] static std::expected<Texture, Error> create(Device&               device,
                                                                  const VriTextureDesc& desc,
                                                                  VriTexture*           borrowed = nullptr,
                                                                  VriImageAspectFlags   aspect = VriImageAspect_Color);
        ~Texture();
        Texture(Texture&& other) noexcept;
        Texture& operator=(Texture&& other) noexcept;
        Texture(const Texture&)            = delete;
        Texture& operator=(const Texture&) = delete;
        // The part must lie within the texture; RenderGraph checks the parts its passes name.
        void                 transition(VriCommandBuffer* cmd, VriAccessLayoutStage next, TextureRange part = {});
        VriAccessLayoutStage state(uint32_t mip = 0, uint32_t layer = 0) const;
        // Array layers, cube faces included; a 3D texture has one.
        uint32_t layerCount() const;
        // Forgets every state: the next transition starts from an undefined layout, as when an external owner such
        // as an XR runtime hands the image back without its previous contents.
        void discardContents();
        // Views are created on first use and owned by the texture. A view of one layer is 2D; any other view has
        // the texture's own type unless `type` names another, such as a 2D array over a cube's faces, which
        // storage access needs.
        [[nodiscard]] std::expected<VriDescriptor*, Error> view(TextureRange                      part = {},
                                                                std::optional<VriTextureViewType> type = std::nullopt);

        VriTexture*    handle = nullptr;
        VriTextureDesc desc {};

    private:
        struct View
        {
            TextureRange       part;
            VriTextureViewType type;
            VriDescriptor*     descriptor;
        };

        Texture(Device& device, const VriTextureDesc& desc, VriTexture* handle, bool owned, VriImageAspectFlags aspect);
        void release();

        Device*                           m_Device;
        bool                              m_Owned;
        VriImageAspectFlags               m_Aspect;
        std::vector<VriAccessLayoutStage> m_States; // Mip-major: mip * layerCount() + layer.
        std::vector<View>                 m_Views;
    };

    // Moving transfers ownership of the VRI buffer; the moved-from buffer is empty.
    class Buffer
    {
    public:
        [[nodiscard]] static std::expected<Buffer, Error> create(Device& device, const VriBufferDesc& desc);
        ~Buffer();
        Buffer(Buffer&& other) noexcept;
        Buffer& operator=(Buffer&& other) noexcept;
        Buffer(const Buffer&)                   = delete;
        Buffer&        operator=(const Buffer&) = delete;
        void           transition(VriCommandBuffer* cmd, VriAccessStage next);
        VriBuffer*     handle = nullptr;
        VriBufferDesc  desc {};
        VriAccessStage state {};

    private:
        Buffer(Device& device, const VriBufferDesc& desc, VriBuffer* handle);

        Device* m_Device;
    };

    VriTextureDesc colorTexture(Extent extent, VriFormat format = VriFormat_RGBA8_UNORM);
    VriTextureDesc depthTexture(Extent extent);
    void           beginColorPass(Device&           device,
                                  VriCommandBuffer* cmd,
                                  VriDescriptor*    view,
                                  Extent            extent,
                                  const float*      clearColor = nullptr);
} // namespace vultra
