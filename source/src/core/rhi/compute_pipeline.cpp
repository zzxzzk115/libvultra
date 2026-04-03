#include "vultra/core/rhi/compute_pipeline.hpp"

#include <glm/common.hpp>
#include <glm/ext/vector_float2.hpp>

namespace vultra
{
    namespace rhi
    {
        glm::uvec3 ComputePipeline::getWorkGroupSize() const
        {
            assert(m_Backend && m_Backend->isValid());
            return m_Backend->getWorkGroupSize();
        }

        ComputePipeline::ComputePipeline(PipelineLayout&&                  pipelineLayout,
                                         const glm::uvec3                  localSize,
                                         const std::uintptr_t              pipeline,
                                         std::unique_ptr<IPipeline>        destroyBackend,
                                         std::unique_ptr<IComputePipeline> backend) :
            BasePipeline(std::move(pipelineLayout), pipeline, std::move(destroyBackend)), m_Backend(std::move(backend)),
            m_LocalSize(localSize)
        {}

        glm::uvec2 calcNumWorkGroups(const glm::uvec2 extent, const glm::uvec2 localSize)
        {
            return glm::ceil(glm::vec2(extent) / glm::vec2(localSize));
        }
    } // namespace rhi
} // namespace vultra
