#include "vultra/core/rhi/compute_pipeline.hpp"

#include <glm/common.hpp>
#include <glm/ext/vector_float2.hpp>

namespace vultra
{
    namespace rhi
    {
        glm::uvec3 ComputePipeline::getWorkGroupSize() const { return m_LocalSize; }

        ComputePipeline::ComputePipeline(const vk::Device   device,
                                         PipelineLayout&&   pipelineLayout,
                                         const glm::uvec3   localSize,
                                         const vk::Pipeline pipeline) :
            BasePipeline(device, std::move(pipelineLayout), pipeline), m_LocalSize(localSize)
        {}

        glm::uvec2 calcNumWorkGroups(const glm::uvec2 extent, const glm::uvec2 localSize)
        {
            return glm::ceil(glm::vec2(extent) / glm::vec2(localSize));
        }
    } // namespace rhi
} // namespace vultra