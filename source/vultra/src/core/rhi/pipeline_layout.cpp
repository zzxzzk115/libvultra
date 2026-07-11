#include "vultra/core/rhi/pipeline_layout.hpp"
#include "vultra/core/base/hash.hpp"
#include "vultra/core/base/ranges.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"

namespace std
{
    template<>
    struct hash<vultra::rhi::DescriptorSetLayoutBindingEx>
    {
        auto operator()(const vultra::rhi::DescriptorSetLayoutBindingEx& v) const noexcept
        {
            size_t h {0};
            hashCombine(h, v.binding, v.type, v.access, v.count, v.stageFlags, v.flags, v.textureType);
            return h;
        }
    };

    template<>
    struct hash<vultra::rhi::PushConstantRange>
    {
        auto operator()(const vultra::rhi::PushConstantRange& v) const noexcept
        {
            size_t h {0};
            hashCombine(h, v.offset, v.size, v.stageFlags);
            return h;
        }
    };
} // namespace std

namespace vultra
{
    namespace rhi
    {

        namespace
        {
            template<typename T>
            using DescriptorContainerSize = std::tuple_size<decltype(T::descriptorSets)>;
        } // namespace

        PipelineLayout::PipelineLayout(PipelineLayout&& other) noexcept : m_Impl(std::move(other.m_Impl)) {}

        PipelineLayout& PipelineLayout::operator=(PipelineLayout&& rhs) noexcept
        {
            if (this != &rhs)
            {
                m_Impl = std::move(rhs.m_Impl);
            }

            return *this;
        }

        PipelineLayout::operator bool() const { return m_Impl && m_Impl->isValid(); }

        std::uintptr_t PipelineLayout::getHandle() const
        {
            assert(m_Impl);
            return m_Impl->getHandle();
        }

        DescriptorSetLayoutKey PipelineLayout::getDescriptorSet(const DescriptorSetIndex index) const
        {
            // An empty pipeline (failed build) has no layout impl; return a null key instead of dereferencing
            // it, so callers that probe a bound-but-invalid pipeline (e.g. pushConstants) bail out safely.
            if (!m_Impl)
                return {};
            return m_Impl->getDescriptorSet(index);
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addImage(const DescriptorSetIndex setIndex,
                                                                   const BindingIndex       bindingIndex,
                                                                   const ShaderStages       stages,
                                                                   const uint32_t           flags)
        {
            return addImages(setIndex, bindingIndex, 1, stages, flags);
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addImages(const DescriptorSetIndex setIndex,
                                                                    const BindingIndex       bindingIndex,
                                                                    const uint32_t           count,
                                                                    const ShaderStages       stages,
                                                                    const uint32_t           flags)
        {
            DescriptorSetLayoutBindingEx desc {};
            desc.binding    = bindingIndex;
            desc.type       = DescriptorType::eStorageImage;
            desc.count      = count;
            desc.stageFlags = stages;
            desc.flags      = flags;
            return addResource(setIndex, desc);
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addSampledImage(const DescriptorSetIndex setIndex,
                                                                          const BindingIndex       bindingIndex,
                                                                          const ShaderStages       stages,
                                                                          const uint32_t           flags)
        {
            return addSampledImages(setIndex, bindingIndex, 1, stages, flags);
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addSampledImages(const DescriptorSetIndex setIndex,
                                                                           const BindingIndex       bindingIndex,
                                                                           const uint32_t           count,
                                                                           const ShaderStages       stages,
                                                                           const uint32_t           flags)
        {
            DescriptorSetLayoutBindingEx desc {};
            desc.binding    = bindingIndex;
            desc.type       = DescriptorType::eCombinedImageSampler;
            desc.count      = count;
            desc.stageFlags = stages;
            desc.flags      = flags;
            return addResource(setIndex, desc);
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addUniformBuffer(const DescriptorSetIndex setIndex,
                                                                           const BindingIndex       bindingIndex,
                                                                           const ShaderStages       stages,
                                                                           const uint32_t           flags)
        {
            DescriptorSetLayoutBindingEx desc {};
            desc.binding    = bindingIndex;
            desc.type       = DescriptorType::eUniformBuffer;
            desc.count      = 1;
            desc.stageFlags = stages;
            desc.flags      = flags;
            return addResource(setIndex, desc);
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addStorageBuffer(const DescriptorSetIndex setIndex,
                                                                           const BindingIndex       bindingIndex,
                                                                           const ShaderStages       stages,
                                                                           const uint32_t           flags)
        {
            DescriptorSetLayoutBindingEx desc {};
            desc.binding    = bindingIndex;
            desc.type       = DescriptorType::eStorageBuffer;
            desc.count      = 1;
            desc.stageFlags = stages;
            desc.flags      = flags;
            return addResource(setIndex, desc);
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addAccelerationStructure(const DescriptorSetIndex setIndex,
                                                                                   const BindingIndex bindingIndex,
                                                                                   const ShaderStages stages,
                                                                                   const uint32_t     flags)
        {
            DescriptorSetLayoutBindingEx desc {};
            desc.binding    = bindingIndex;
            desc.type       = DescriptorType::eAccelerationStructure;
            desc.count      = 1;
            desc.stageFlags = stages;
            desc.flags      = flags;
            return addResource(setIndex, desc);
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addResource(const DescriptorSetIndex     index,
                                                                      DescriptorSetLayoutBindingEx desc)
        {
            assert(index <= m_LayoutInfo.descriptorSets.size());
            m_LayoutInfo.descriptorSets[index].emplace_back(std::move(desc));
            return *this;
        }

        PipelineLayout::Builder& PipelineLayout::Builder::addPushConstantRange(PushConstantRange pushConstantRange)
        {
            assert(pushConstantRange.stageFlags != ShaderStages::eNone);
            m_LayoutInfo.pushConstantRanges.emplace_back(std::move(pushConstantRange));
            return *this;
        }

        PipelineLayout PipelineLayout::Builder::build(RenderDevice& rd) const
        {
            return rd.createPipelineLayout(m_LayoutInfo);
        }

        PipelineLayout::PipelineLayout(std::unique_ptr<IPipelineLayout> impl) : m_Impl(std::move(impl)) {}

        PipelineLayout reflectPipelineLayout(RenderDevice& rd, const ShaderReflection& reflection)
        {
            PipelineLayout::Builder builder {};

            for (const auto& [set, bindings] : vultra::enumerate(reflection.descriptorSets))
            {
                for (const auto& [index, resource] : bindings)
                {
                    DescriptorSetLayoutBindingEx desc {};
                    desc.binding    = index;
                    desc.type       = resource.type;
                    desc.access     = resource.access;
                    desc.count      = resource.count;
                    desc.stageFlags = resource.stageFlags;
                    desc.flags      = resource.flags;
                    desc.textureType = resource.textureType;
                    desc.depthSampled = resource.depthSampled;
                    builder.addResource(static_cast<DescriptorSetIndex>(set), desc);
                }
            }
            for (const auto& range : reflection.pushConstantRanges)
            {
                builder.addPushConstantRange(PushConstantRange {range.offset, range.size, range.stageFlags});
            }
            return builder.build(rd);
        }
    } // namespace rhi
} // namespace vultra
