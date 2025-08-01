#include "vultra/core/rhi/framebuffer_info.hpp"
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
                    v, out.begin(), [](const auto& attachment) { return attachment.target->getPixelFormat(); });
                return out;
            }

        } // namespace

        PixelFormat getDepthFormat(const FramebufferInfo& info)
        {
            return info.depthAttachment ? info.depthAttachment->target->getPixelFormat() : PixelFormat::eUndefined;
        }
        PixelFormat getColorFormat(const FramebufferInfo& info, const AttachmentIndex index)
        {
            assert(index < info.colorAttachments.size());
            return info.colorAttachments[index].target->getPixelFormat();
        }

        std::vector<PixelFormat> getColorFormats(const FramebufferInfo& info)
        {
            return getColorFormats(info.colorAttachments);
        }
    } // namespace rhi
} // namespace vultra