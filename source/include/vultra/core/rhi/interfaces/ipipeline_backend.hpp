#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class IPipelineBackend
        {
        public:
            virtual ~IPipelineBackend() = default;

            virtual void destroy(std::uintptr_t pipelineHandle) noexcept = 0;
        };
    } // namespace rhi
} // namespace vultra
