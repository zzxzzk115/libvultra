#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/base/ranges.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            template<typename T>
            using DescriptorContainerSize = std::tuple_size<decltype(T::descriptorSets)>;
        } // namespace

        PipelineLayout::PipelineLayout(PipelineLayout&& other) noexcept :
            m_Handle(other.m_Handle), m_DescriptorSetLayouts(std::move(other.m_DescriptorSetLayouts))
        {
            other.m_Handle = nullptr;
        }

        PipelineLayout& PipelineLayout::operator=(PipelineLayout&& rhs) noexcept
        {
            if (this != &rhs)
            {
                m_Handle               = std::exchange(rhs.m_Handle, nullptr);
                m_DescriptorSetLayouts = std::move(rhs.m_DescriptorSetLayouts);
            }

            return *this;
        }

        PipelineLayout::operator bool() const { return m_Handle != nullptr; }

        vk::PipelineLayout PipelineLayout::getHandle() const { return m_Handle; }

        vk::DescriptorSetLayout PipelineLayout::getDescriptorSet(const DescriptorSetIndex index) const
        {
            assert(index < m_DescriptorSetLayouts.size());

            return m_DescriptorSetLayouts[index];
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addImage(const DescriptorSetIndex   setIndex,
                                                                   const BindingIndex         bindingIndex,
                                                                   const vk::ShaderStageFlags stages)
        {
            return addImages(setIndex, bindingIndex, 1, stages);
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addImages(const DescriptorSetIndex   setIndex,
                                                                    const BindingIndex         bindingIndex,
                                                                    const uint32_t             count,
                                                                    const vk::ShaderStageFlags stages)
        {
            return addResource(
                setIndex,
                vk::DescriptorSetLayoutBinding {bindingIndex, vk::DescriptorType::eStorageImage, count, stages});
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addSampledImage(const DescriptorSetIndex   setIndex,
                                                                          const BindingIndex         bindingIndex,
                                                                          const vk::ShaderStageFlags stages)
        {
            return addSampledImages(setIndex, bindingIndex, 1, stages);
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addSampledImages(const DescriptorSetIndex   setIndex,
                                                                           const BindingIndex         bindingIndex,
                                                                           const uint32_t             count,
                                                                           const vk::ShaderStageFlags stages)
        {
            return addResource(setIndex,
                               vk::DescriptorSetLayoutBinding {
                                   bindingIndex, vk::DescriptorType::eCombinedImageSampler, count, stages});
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addUniformBuffer(const DescriptorSetIndex   setIndex,
                                                                           const BindingIndex         bindingIndex,
                                                                           const vk::ShaderStageFlags stages)
        {
            return addResource(
                setIndex, vk::DescriptorSetLayoutBinding {bindingIndex, vk::DescriptorType::eUniformBuffer, 1, stages});
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addStorageBuffer(const DescriptorSetIndex   setIndex,
                                                                           const BindingIndex         bindingIndex,
                                                                           const vk::ShaderStageFlags stages)
        {
            return addResource(
                setIndex, vk::DescriptorSetLayoutBinding {bindingIndex, vk::DescriptorType::eStorageBuffer, 1, stages});
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addResource(const DescriptorSetIndex       index,
                                                                      vk::DescriptorSetLayoutBinding desc)
        {
            assert(index <= m_LayoutInfo.descriptorSets.size());
            m_LayoutInfo.descriptorSets[index].emplace_back(std::move(desc));
            return *this;
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addPushConstantRange(vk::PushConstantRange pushConstantRange)
        {
            assert(pushConstantRange.stageFlags & vk::ShaderStageFlagBits::eAll);
            m_LayoutInfo.pushConstantRanges.emplace_back(std::move(pushConstantRange));
            return *this;
        }

        PipelineLayout PipelineLayout::Builder::build(RenderDevice& rd) const
        {
            return rd.createPipelineLayout(m_LayoutInfo);
        }

        PipelineLayout::PipelineLayout(const vk::PipelineLayout               handle,
                                       std::vector<vk::DescriptorSetLayout>&& descriptorSetLayouts) :
            m_Handle(handle), m_DescriptorSetLayouts(std::move(descriptorSetLayouts))
        {}

        PipelineLayout reflectPipelineLayout(RenderDevice& rd, const ShaderReflection& reflection)
        {
            PipelineLayout::Builder builder {};

            for (const auto& [set, bindings] : vultra::enumerate(reflection.descriptorSets))
            {
                for (const auto& [index, resource] : bindings)
                {
                    builder.addResource(static_cast<DescriptorSetIndex>(set),
                                        vk::DescriptorSetLayoutBinding {
                                            index,
                                            resource.type,
                                            resource.count,
                                            resource.stageFlags,
                                        });
                }
            }
            for (const auto& range : reflection.pushConstantRanges)
            {
                builder.addPushConstantRange(range);
            }
            return builder.build(rd);
        }
    } // namespace rhi
} // namespace vultra