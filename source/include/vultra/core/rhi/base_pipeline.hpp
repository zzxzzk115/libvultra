#pragma once

#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/rhi/structs/shader_stage_info.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        enum class PipelineBindPoint
        {
            eGraphics,
            eCompute,
            eRayTracing,
        };

        class BasePipeline
        {
        public:
            BasePipeline()                    = default;
            BasePipeline(const BasePipeline&) = delete;
            BasePipeline(BasePipeline&&) noexcept;
            virtual ~BasePipeline();

            BasePipeline& operator=(const BasePipeline&) = delete;
            BasePipeline& operator=(BasePipeline&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] std::uintptr_t getHandle() const;
            [[nodiscard]] constexpr virtual PipelineBindPoint getBindPoint() const = 0;

            [[nodiscard]] const PipelineLayout&   getLayout() const;
            [[nodiscard]] std::uintptr_t getDescriptorSetLayout(const DescriptorSetIndex) const;

        protected:
            BasePipeline(std::uintptr_t, PipelineLayout&&, std::uintptr_t);

        private:
            void destroy() noexcept;

        private:
            std::uintptr_t m_Device {0};
            PipelineLayout m_Layout;
            std::uintptr_t m_Handle {0};
        };
    } // namespace rhi
} // namespace vultra
