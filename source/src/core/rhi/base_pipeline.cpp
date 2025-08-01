#include "vultra/core/rhi/base_pipeline.hpp"

namespace vultra
{
    namespace rhi
    {
        BasePipeline::BasePipeline(BasePipeline&& other) noexcept :
            m_Device(other.m_Device), m_Layout(std::move(other.m_Layout)), m_Handle(other.m_Handle)
        {
            other.m_Device = nullptr;
            other.m_Handle = nullptr;
        }

        BasePipeline::~BasePipeline() { destroy(); }

        BasePipeline& BasePipeline::operator=(BasePipeline&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy();

                std::swap(m_Device, rhs.m_Device);
                m_Layout = std::move(rhs.m_Layout);
                std::swap(m_Handle, rhs.m_Handle);
            }

            return *this;
        }

        BasePipeline::operator bool() const { return m_Handle != nullptr; }

        vk::Pipeline BasePipeline::getHandle() const { return m_Handle; }

        const PipelineLayout& BasePipeline::getLayout() const { return m_Layout; }

        vk::DescriptorSetLayout BasePipeline::getDescriptorSetLayout(const DescriptorSetIndex index) const
        {
            return m_Layout.getDescriptorSet(index);
        }

        BasePipeline::BasePipeline(const vk::Device device, PipelineLayout&& layout, const vk::Pipeline pipeline) :
            m_Device(device), m_Layout(std::move(layout)), m_Handle(pipeline)
        {
            assert(device);
        }

        void BasePipeline::destroy() noexcept
        {
            if (!m_Handle)
            {
                return;
            }

            m_Device.destroyPipeline(m_Handle);

            m_Device = nullptr;
            m_Handle = nullptr;
        }
    } // namespace rhi
} // namespace vultra