#pragma once

#include "vultra/core/rhi/interfaces/ipipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        class WebGPUPipeline final : public IPipeline
        {
        public:
            void destroy(std::uintptr_t pipelineHandle) noexcept override;
        };
    } // namespace rhi
} // namespace vultra

