#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/storage_buffer.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace vultra::resource
{
    // Global GPU material parameter pool.
    //
    // - Sync baseline: allocate + upload immediately.
    // - Async later: worker builds CPU blob; main thread performs one upload per frame.
    //
    // NOTE: This buffer stores tightly packed material parameter blocks.
    // The packing strategy will later be driven by vshadersystem reflection.
    struct MaterialBuffer
    {
        Ref<rhi::StorageBuffer> gpu {nullptr};
        std::vector<std::byte>  cpu;
        uint32_t                cursorBytes {0};

        static constexpr uint32_t s_DefaultAlign = 16;

        void reset()
        {
            cpu.clear();
            cursorBytes = 0;
            gpu         = nullptr;
        }

        static uint32_t alignUp(uint32_t v, uint32_t align) { return (v + align - 1u) & ~(align - 1u); }

        // Allocates a block, writes data into CPU mirror, grows GPU buffer as needed, and uploads.
        // Returns byte offset into the material buffer.
        uint32_t
        allocAndUpload(rhi::RenderDevice& rd, const void* src, uint32_t sizeBytes, uint32_t alignBytes = s_DefaultAlign)
        {
            const uint32_t alignedSize = alignUp(sizeBytes, alignBytes);
            const uint32_t offset      = alignUp(cursorBytes, alignBytes);

            const size_t required = offset + alignedSize;
            if (cpu.size() < required)
                cpu.resize(required);

            if (sizeBytes > 0)
                std::memcpy(cpu.data() + offset, src, sizeBytes);
            if (alignedSize > sizeBytes)
                std::memset(cpu.data() + offset + sizeBytes, 0, alignedSize - sizeBytes);

            cursorBytes = offset + alignedSize;

            // Lazy create / grow GPU buffer.
            if (!gpu || gpu->getSize() < cpu.size())
            {
                gpu = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(cpu.size()));
            }

            // Sync baseline: upload full blob.
            rd.upload(*gpu, 0, cpu.size(), cpu.data());

            return offset;
        }
    };
} // namespace vultra::resource
