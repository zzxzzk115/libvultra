#pragma once

#include <cstdint>
#include <glm/ext/vector_uint3.hpp>

namespace vultra
{
    namespace rhi
    {
        class IComputePipelineBackend
        {
        public:
            virtual ~IComputePipelineBackend() = default;

            [[nodiscard]] virtual bool      isValid() const = 0;
            [[nodiscard]] virtual std::uintptr_t getHandle() const = 0;
            [[nodiscard]] virtual glm::uvec3 getWorkGroupSize() const = 0;
        };
    } // namespace rhi
} // namespace vultra
