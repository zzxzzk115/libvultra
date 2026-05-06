#pragma once

#include "vultra/core/rhi/interfaces/ipipeline.hpp"
#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/rhi/structs/pipeline_bind_point.hpp"
#include "vultra/core/rhi/structs/shader_stage_info.hpp"

#include <cstdint>
#include <memory>

namespace vultra
{
    namespace rhi
    {
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

            [[nodiscard]] std::uintptr_t                      getHandle() const;
            [[nodiscard]] constexpr virtual PipelineBindPoint getBindPoint() const = 0;

            [[nodiscard]] const PipelineLayout&  getLayout() const;
            [[nodiscard]] DescriptorSetLayoutKey getDescriptorSetLayout(const DescriptorSetIndex) const;

        protected:
            BasePipeline(PipelineLayout&&, std::uintptr_t, std::unique_ptr<IPipeline>);

        private:
            void destroy() noexcept;

        private:
            PipelineLayout             m_Layout;
            std::uintptr_t             m_Handle {0};
            std::unique_ptr<IPipeline> m_Backend;
        };
    } // namespace rhi
} // namespace vultra
