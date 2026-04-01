#pragma once

namespace vultra
{
    namespace rhi
    {
        enum class ImageLayout
        {
            eUndefined,
            eGeneral,
            eAttachment,
            eReadOnly,
            eTransferSrc,
            eTransferDst,
            ePresent,
        };
    } // namespace rhi
} // namespace vultra
