#pragma once

#include "vultra/core/rhi/structs/pipeline_layout_structs.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class IPipelineLayout
        {
        public:
            virtual ~IPipelineLayout() = default;

            [[nodiscard]] virtual bool isValid() const = 0;
            [[nodiscard]] virtual std::uintptr_t getHandle() const = 0;
            [[nodiscard]] virtual DescriptorSetLayoutKey getDescriptorSet(DescriptorSetIndex) const = 0;
        };
    } // namespace rhi
} // namespace vultra
