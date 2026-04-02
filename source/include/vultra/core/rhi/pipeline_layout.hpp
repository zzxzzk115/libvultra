#pragma once

#include "vultra/core/rhi/structs/pipeline_layout_structs.hpp"

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        class PipelineLayout final
        {
            friend class RenderDevice;

        public:
            PipelineLayout()                      = default;
            PipelineLayout(const PipelineLayout&) = default;
            PipelineLayout(PipelineLayout&&) noexcept;
            ~PipelineLayout() = default;

            PipelineLayout& operator=(const PipelineLayout&) = default;
            PipelineLayout& operator=(PipelineLayout&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] std::uintptr_t getHandle() const;
            [[nodiscard]] DescriptorSetLayoutKey getDescriptorSet(const DescriptorSetIndex) const;

            class Builder
            {
            public:
                Builder()                   = default;
                Builder(const Builder&)     = delete;
                Builder(Builder&&) noexcept = delete;
                ~Builder()                  = default;

                Builder& operator=(const Builder&)     = delete;
                Builder& operator=(Builder&&) noexcept = delete;

                Builder& addImage(const DescriptorSetIndex, const BindingIndex, ShaderStages, const uint32_t = {});
                Builder& addImages(const DescriptorSetIndex,
                                   const BindingIndex,
                                   uint32_t count,
                                   ShaderStages,
                                   const uint32_t = {});
                Builder& addSampledImage(const DescriptorSetIndex, const BindingIndex, ShaderStages, const uint32_t = {});
                Builder& addSampledImages(const DescriptorSetIndex,
                                          const BindingIndex,
                                          uint32_t count,
                                          ShaderStages,
                                          const uint32_t = {});
                Builder& addUniformBuffer(const DescriptorSetIndex, const BindingIndex, ShaderStages, const uint32_t = {});
                Builder& addStorageBuffer(const DescriptorSetIndex, const BindingIndex, ShaderStages, const uint32_t = {});

                Builder& addAccelerationStructure(const DescriptorSetIndex,
                                                  const BindingIndex,
                                                  ShaderStages,
                                                  const uint32_t = {});

                Builder& addResource(const DescriptorSetIndex, DescriptorSetLayoutBindingEx);
                Builder& addPushConstantRange(PushConstantRange);

                [[nodiscard]] PipelineLayout build(RenderDevice&) const;

            private:
                PipelineLayoutInfo m_LayoutInfo;
            };

        private:
            PipelineLayout(std::uintptr_t, std::vector<DescriptorSetLayoutKey>&&);

        private:
            std::uintptr_t                       m_Handle {0}; // Non-owning.
            std::vector<DescriptorSetLayoutKey> m_DescriptorSetLayouts;
        };

        struct ShaderReflection;

        [[nodiscard]] PipelineLayout reflectPipelineLayout(RenderDevice&, const ShaderReflection&);

    } // namespace rhi
} // namespace vultra
