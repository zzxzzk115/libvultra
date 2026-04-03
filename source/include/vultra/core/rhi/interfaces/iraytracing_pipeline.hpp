#pragma once

#include "vultra/core/rhi/structs/raytracing_pipeline_properties.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class IRayTracingPipeline
        {
        public:
            virtual ~IRayTracingPipeline() = default;

            [[nodiscard]] virtual bool                         isValid() const = 0;
            [[nodiscard]] virtual std::uintptr_t              getHandle() const = 0;
            [[nodiscard]] virtual RayTracingPipelineProperties getProperties() const = 0;
        };
    } // namespace rhi
} // namespace vultra
