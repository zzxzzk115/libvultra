#pragma once

#include "vultra/core/rhi/interfaces/ipipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        class WebGPUComputePipelineDestroy final : public IPipeline
        {
        public:
            void destroy(std::uintptr_t handle) noexcept override;
        };
    } // namespace rhi
} // namespace vultra
