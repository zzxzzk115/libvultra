#pragma once

#include <vultra/core/image/image.hpp>
#include <vultra/drivers/rhi/resources.hpp>

#include <expected>
#include <memory>
#include <span>
#include <string>

namespace vultra
{
    struct TextureReadbackData
    {
        VriFormat            format = VriFormat_Unknown;
        VriTextureRegionDesc region {};
        // Native texel/block encoding, tightly packed rows and depth slices, without a color transform.
        std::vector<std::byte> bytes;
    };

    class TextureReadback
    {
    public:
        // One selected mip/layer/plane, including the complete 3D mip depth. Combined depth/stencil needs
        // an explicit single aspect. The device and source survive final GPU use.
        static std::expected<std::unique_ptr<TextureReadback>, std::string>
        create(Device&             device,
               Texture&            texture,
               uint32_t            mip    = 0,
               uint32_t            layer  = 0,
               VriImageAspectFlags aspect = VriImageAspect_None);
        // Submit on the graphics queue without a CPU wait. Own staging and completion until destruction.
        // Native waits/signals express dependencies on other queues; their owners survive completion too.
        static std::expected<std::unique_ptr<TextureReadback>, std::string>
        start(Device&                             device,
              Texture&                            texture,
              uint32_t                            mip     = 0,
              uint32_t                            layer   = 0,
              VriImageAspectFlags                 aspect  = VriImageAspect_None,
              std::span<const VriFenceSubmitDesc> waits   = {},
              std::span<const VriFenceSubmitDesc> signals = {});
        ~TextureReadback();
        TextureReadback(const TextureReadback&)            = delete;
        TextureReadback& operator=(const TextureReadback&) = delete;

        // create() mode only: record into the caller's command buffer. Complete/discard previous work before
        // recording again and establish completion before dataExpected(). Failure requires command discard.
        std::expected<void, std::string> recordExpected(VriCommandBuffer* commands, Texture& texture);
        // start() mode only. Timeout/error retains the submitted work and its owners for another wait.
        std::expected<void, std::string> waitExpected(uint64_t timeoutNs = UINT64_MAX);
        // start() waits first; create() relies on caller-established completion. Repeated reads are supported.
        std::expected<TextureReadbackData, std::string> dataExpected(uint64_t timeoutNs = UINT64_MAX);

    private:
        friend class ImageReadback;
        static std::expected<std::unique_ptr<TextureReadback>, std::string>
        createSelectionExpected(Device&             device,
                                Texture&            texture,
                                uint32_t            mip,
                                uint32_t            layer,
                                VriImageAspectFlags aspect,
                                bool                sliceZero);
        TextureReadback(Device&                  device,
                        VriFormat                format,
                        VriBufferTextureCopyDesc copy,
                        uint32_t                 rows,
                        uint64_t                 rowBytes,
                        uint64_t                 packedRowBytes,
                        bool                     sliceZero,
                        std::unique_ptr<Buffer>  staging);
        std::expected<void, std::string> recordIntoExpected(VriCommandBuffer* commands, Texture& texture);
        Device&                          m_Device;
        VriFormat                        m_Format;
        VriBufferTextureCopyDesc         m_Copy;
        uint32_t                         m_Rows;
        uint64_t                         m_RowBytes;
        uint64_t                         m_PackedRowBytes;
        bool                             m_Recorded  = false;
        bool                             m_SliceZero = false;
        std::unique_ptr<Buffer>          m_Staging;
        // Destroy first: an accepted copy must complete before its staging buffer is released.
        std::unique_ptr<Frame> m_Frame;
    };

    // Float-image adapter. Retains the legacy RGBA/BGRA8, RGBA16/32F and D32 formats and 3D slice-zero view.
    // Record into the caller's frame; consume only after its submission completes.
    class ImageReadback
    {
    public:
        static std::expected<std::unique_ptr<ImageReadback>, std::string>
        create(Device& device, Texture& texture, uint32_t mip = 0);
        ImageReadback(Device& device, Texture& texture, uint32_t mip = 0);
        std::expected<void, std::string>  recordExpected(VriCommandBuffer* commands, Texture& texture);
        std::expected<Image, std::string> consumeExpected();
        // Call only after discarding an unsubmitted recording. GPU state is restored by recordExpected().
        void  discardUnsubmitted();
        void  record(VriCommandBuffer* commands, Texture& texture); // Legacy boundary.
        Image consume();                                            // Legacy boundary.

    private:
        explicit ImageReadback(std::unique_ptr<TextureReadback> readback);
        std::unique_ptr<TextureReadback> m_Readback;
        bool                             m_Recorded = false;
    };

    std::expected<Image, std::string> readbackExpected(Device& device, Texture& texture, uint32_t mip = 0);
    // Blocking float-image readback, without implicit gamma/tone mapping. Source needs initialized TransferSrc.
    Image readback(Device& device, Texture& texture, uint32_t mip = 0); // Legacy boundary.
} // namespace vultra
