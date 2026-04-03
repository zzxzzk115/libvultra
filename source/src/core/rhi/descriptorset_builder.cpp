#include "vultra/core/rhi/descriptorset_builder.hpp"

#include "vultra/core/base/visitor_helper.hpp"

namespace vultra
{
    namespace rhi
    {
        DescriptorSetBuilder::DescriptorSetBuilder(std::unique_ptr<IDescriptorSetBuilder> impl) :
            m_Impl(std::move(impl))
        {
            assert(m_Impl);
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const ResourceBinding& r)
        {
            assert(m_Impl);
            m_Impl->bind(index, r);
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::SeparateSampler& info)
        {
            assert(m_Impl);
            m_Impl->bind(index, info);
            return *this;
        }

        DescriptorSetBuilder&
        DescriptorSetBuilder::bind(const BindingIndex index, const bindings::CombinedImageSampler& info)
        {
            assert(m_Impl);
            m_Impl->bind(index, info);
            return *this;
        }

        DescriptorSetBuilder&
        DescriptorSetBuilder::bind(const BindingIndex index, const bindings::CombinedImageSamplerArray& info)
        {
            assert(m_Impl);
            m_Impl->bind(index, info);
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::SampledImage& info)
        {
            assert(m_Impl);
            m_Impl->bind(index, info);
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::StorageImage& info)
        {
            assert(m_Impl);
            m_Impl->bind(index, info);
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::UniformBuffer& info)
        {
            assert(m_Impl);
            m_Impl->bind(index, info);
            return *this;
        }

        DescriptorSetBuilder& DescriptorSetBuilder::bind(const BindingIndex index, const bindings::StorageBuffer& info)
        {
            assert(m_Impl);
            m_Impl->bind(index, info);
            return *this;
        }

        DescriptorSetBuilder&
        DescriptorSetBuilder::bind(const BindingIndex index, const bindings::AccelerationStructureKHR& info)
        {
            assert(m_Impl);
            m_Impl->bind(index, info);
            return *this;
        }

        DescriptorSetHandle DescriptorSetBuilder::build(const DescriptorSetLayoutKey layoutKey)
        {
            assert(m_Impl);
            return m_Impl->build(layoutKey);
        }

        std::string_view toString(const ResourceBinding& rb)
        {
#define CASE(T) [](const bindings::T&) { return #T; }

            return std::visit(
                Overload {
                    CASE(SeparateSampler),
                    CASE(CombinedImageSampler),
                    CASE(CombinedImageSamplerArray),
                    CASE(SampledImage),
                    CASE(StorageImage),
                    CASE(UniformBuffer),
                    CASE(StorageBuffer),
                    CASE(AccelerationStructureKHR),
                },
                rb);

#undef CASE
        }
    } // namespace rhi
} // namespace vultra
