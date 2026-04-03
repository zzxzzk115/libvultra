#include "vultra/core/rhi/base_pipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        BasePipeline::BasePipeline(BasePipeline&& other) noexcept :
            m_Layout(std::move(other.m_Layout)), m_Handle(other.m_Handle), m_Backend(std::move(other.m_Backend))
        {
            other.m_Handle = 0;
        }

        BasePipeline::~BasePipeline() { destroy(); }

        BasePipeline& BasePipeline::operator=(BasePipeline&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();

                m_Layout = std::move(rhs.m_Layout);
                std::swap(m_Handle, rhs.m_Handle);
                std::swap(m_Backend, rhs.m_Backend);
            }

            return *this;
        }

        BasePipeline::operator bool() const { return m_Handle != 0; }

        std::uintptr_t BasePipeline::getHandle() const { return m_Handle; }

        const PipelineLayout& BasePipeline::getLayout() const { return m_Layout; }

        DescriptorSetLayoutKey BasePipeline::getDescriptorSetLayout(const DescriptorSetIndex index) const
        {
            return m_Layout.getDescriptorSet(index);
        }

        BasePipeline::BasePipeline(PipelineLayout&&           layout,
                                   const std::uintptr_t       pipeline,
                                   std::unique_ptr<IPipeline> destroyBackend) :
            m_Layout(std::move(layout)), m_Handle(pipeline), m_Backend(std::move(destroyBackend))
        {
            assert(m_Backend);
        }

        void BasePipeline::destroy() noexcept
        {
            if (!m_Handle)
            {
                return;
            }

            m_Backend->destroy(m_Handle);
            m_Handle = 0;
        }
    } // namespace rhi
} // namespace vultra
