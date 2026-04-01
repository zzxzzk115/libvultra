#include "vultra/core/rhi/base_pipeline.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        BasePipeline::BasePipeline(BasePipeline&& other) noexcept :
            m_Device(other.m_Device), m_Layout(std::move(other.m_Layout)), m_Handle(other.m_Handle)
        {
            other.m_Device = 0;
            other.m_Handle = 0;
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

        BasePipeline::operator bool() const { return m_Handle != 0; }

        std::uintptr_t BasePipeline::getHandle() const { return m_Handle; }

        const PipelineLayout& BasePipeline::getLayout() const { return m_Layout; }

        std::uintptr_t BasePipeline::getDescriptorSetLayout(const DescriptorSetIndex index) const
        {
            return m_Layout.getDescriptorSet(index);
        }

        BasePipeline::BasePipeline(const std::uintptr_t device, PipelineLayout&& layout, const std::uintptr_t pipeline) :
            m_Device(device), m_Layout(std::move(layout)), m_Handle(pipeline)
        {
            assert(device != 0);
        }

        void BasePipeline::destroy() noexcept
        {
            if (!m_Handle)
            {
                return;
            }

            auto device = vk::Device {reinterpret_cast<VkDevice>(m_Device)};
            device.destroyPipeline(vk::Pipeline {reinterpret_cast<VkPipeline>(m_Handle)});

            m_Device = 0;
            m_Handle = 0;
        }
    } // namespace rhi
} // namespace vultra
