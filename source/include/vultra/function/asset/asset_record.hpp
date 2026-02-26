#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/function/asset/asset_state.hpp"

#include <atomic>
#include <limits>
#include <memory>

namespace vultra
{
    template<class TCpu, class TGpu>
    struct AssetRecord
    {
        using CpuType = TCpu;
        using GpuType = TGpu;

        CoreUUID uuid {};

        std::atomic<AssetState> state {AssetState::eUnloaded};
        std::atomic<uint32_t>   refCount {0};

        // Deferred unloading
        std::atomic<uint64_t> lastUsedFrame {0};

        // CPU side (owned)
        std::unique_ptr<TCpu> cpu {};

        // GPU side: index into the owning GPU table (GpuScene).
        // We intentionally do NOT own GPU resources here.
        // UINT32_MAX means "not resident".
        std::atomic<uint32_t> gpuIndex {std::numeric_limits<uint32_t>::max()};

        // Upload scheduling (thread-safe).
        // Multiple threads may request the same asset; this flag ensures we enqueue at most one GPU upload command.
        std::atomic_bool uploadQueued {false};

        // Optional diagnostics
        std::atomic<int32_t> errorCode {0};
    };
} // namespace vultra