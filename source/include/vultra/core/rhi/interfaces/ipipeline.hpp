#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class IPipeline
        {
        public:
            virtual ~IPipeline() = default;

            virtual void destroy(std::uintptr_t pipelineHandle) noexcept = 0;
        };
    } // namespace rhi
} // namespace vultra
