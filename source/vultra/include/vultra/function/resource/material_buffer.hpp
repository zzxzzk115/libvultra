#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
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
    // Shader-backed materials use vshadersystem reflection offsets; builtin
    // paths keep their existing packed structs.
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

        uint32_t
        allocAndUpload(rhi::RenderDevice& rd, const void* src, uint32_t sizeBytes, uint32_t alignBytes = s_DefaultAlign)
        {
            const uint32_t alignedSize = alignUp(sizeBytes, alignBytes);
            const uint32_t offset      = alignUp(cursorBytes, alignBytes);
            const size_t   required    = offset + alignedSize;

            if (cpu.size() < required)
                cpu.resize(required);

            // copy into CPU mirror
            if (sizeBytes)
                std::memcpy(cpu.data() + offset, src, sizeBytes);

            if (alignedSize > sizeBytes)
                std::memset(cpu.data() + offset + sizeBytes, 0, alignedSize - sizeBytes);

            cursorBytes = offset + alignedSize;

            // ============================================================
            // create / grow GPU buffer (DEVICE LOCAL)
            // ============================================================
            if (!gpu || gpu->getSize() < cpu.size())
            {
                gpu = createRef<rhi::StorageBuffer>(rd.createStorageBuffer(cpu.size()));
            }

            // ============================================================
            // STAGING upload
            // ============================================================
            rd.uploadS(*gpu, 0, static_cast<uint64_t>(cpu.size()), cpu.data());

            return offset;
        }
    };
} // namespace vultra::resource
