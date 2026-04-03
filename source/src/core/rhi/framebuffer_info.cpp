#include "vultra/core/rhi/structs/framebuffer_info.hpp"
#include "vultra/core/rhi/texture.hpp"

#include <algorithm>
#include <span>

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] auto getColorFormats(std::span<const AttachmentInfo> v)
            {
                std::vector<PixelFormat> out(v.size());
                std::ranges::transform(
                    v,
                    out.begin(),
                    [](const auto& attachment) {
                        return attachment.target ? attachment.target->getPixelFormat() : PixelFormat::eUndefined;
                    });
                return out;
            }

        } // namespace

        PixelFormat getDepthFormat(const FramebufferInfo& info)
        {
            if (!info.depthAttachment || !info.depthAttachment->target)
            {
                return PixelFormat::eUndefined;
            }
            return info.depthAttachment->target->getPixelFormat();
        }
        PixelFormat getColorFormat(const FramebufferInfo& info, const AttachmentIndex index)
        {
            assert(index < info.colorAttachments.size());
            const auto* target = info.colorAttachments[index].target;
            return target ? target->getPixelFormat() : PixelFormat::eUndefined;
        }

        std::vector<PixelFormat> getColorFormats(const FramebufferInfo& info)
        {
            return getColorFormats(info.colorAttachments);
        }
    } // namespace rhi
} // namespace vultra
