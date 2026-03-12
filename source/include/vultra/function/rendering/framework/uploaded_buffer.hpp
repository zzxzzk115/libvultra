#pragma once

#include "vultra/core/rhi/buffer.hpp"

#include <fg/FrameGraphResource.hpp>

namespace vultra
{
    struct UploadedBuffer
    {
        enum class Kind
        {
            eInvalid,
            eFrameGraph,
            eImmediate
        };

        Kind kind {Kind::eInvalid};

        FrameGraphResource fgResource {};
        rhi::Buffer*       buffer {nullptr};

        [[nodiscard]] bool valid() const { return kind != Kind::eInvalid; }

        [[nodiscard]] bool isFrameGraph() const { return kind == Kind::eFrameGraph; }

        [[nodiscard]] bool isImmediate() const { return kind == Kind::eImmediate; }
    };
} // namespace vultra
