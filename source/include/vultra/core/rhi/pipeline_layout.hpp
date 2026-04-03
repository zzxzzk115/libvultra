#pragma once

#include "vultra/core/rhi/interfaces/ipipeline_layout.hpp"
#include "vultra/core/rhi/structs/pipeline_layout_structs.hpp"

#include <cstdint>
#include <memory>

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
            PipelineLayout(const PipelineLayout&) = delete;
            PipelineLayout(PipelineLayout&&) noexcept;
            ~PipelineLayout() = default;

            PipelineLayout& operator=(const PipelineLayout&) = delete;
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
            explicit PipelineLayout(std::unique_ptr<IPipelineLayout>);

        private:
            std::unique_ptr<IPipelineLayout> m_Impl;
        };

        struct ShaderReflection;

        [[nodiscard]] PipelineLayout reflectPipelineLayout(RenderDevice&, const ShaderReflection&);

    } // namespace rhi
} // namespace vultra
