#pragma once

#include "vultra/core/rhi/base_pipeline.hpp"

#include <glm/ext/vector_uint2.hpp>
#include <glm/ext/vector_uint3.hpp>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        class ComputePipeline final : public BasePipeline
        {
            friend class RenderDevice;

        public:
            ComputePipeline()                           = default;
            ComputePipeline(const ComputePipeline&)     = delete;
            ComputePipeline(ComputePipeline&&) noexcept = default;

            ComputePipeline& operator=(const ComputePipeline&)     = delete;
            ComputePipeline& operator=(ComputePipeline&&) noexcept = default;

            constexpr vk::PipelineBindPoint getBindPoint() const override { return vk::PipelineBindPoint::eCompute; }

            [[nodiscard]] glm::uvec3 getWorkGroupSize() const;

        private:
            ComputePipeline(const vk::Device, PipelineLayout&&, const glm::uvec3 localSize, const vk::Pipeline);

        private:
            glm::uvec3 m_LocalSize {};
        };

        [[nodiscard]] glm::uvec2  calcNumWorkGroups(const glm::uvec2 extent, const glm::uvec2 localSize);
        [[nodiscard]] inline auto calcNumWorkGroups(const glm::uvec2 extent, const uint32_t localSize)
        {
            return calcNumWorkGroups(extent, glm::uvec2 {localSize});
        }
    } // namespace rhi
} // namespace vultra