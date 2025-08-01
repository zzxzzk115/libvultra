#pragma once

#include "vultra/core/rhi/pipeline_layout.hpp"

#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        struct ShaderStageInfo
        {
            std::string                                                 code;
            std::string                                                 entryPointName {"main"};
            std::unordered_map<std::string, std::optional<std::string>> defines;
        };

        class BasePipeline
        {
        public:
            BasePipeline()                    = default;
            BasePipeline(const BasePipeline&) = delete;
            BasePipeline(BasePipeline&&) noexcept;
            virtual ~BasePipeline();

            BasePipeline& operator=(const BasePipeline&) = delete;
            BasePipeline& operator=(BasePipeline&&) noexcept;

            [[nodiscard]] explicit operator bool() const;

            [[nodiscard]] vk::Pipeline                            getHandle() const;
            [[nodiscard]] constexpr virtual vk::PipelineBindPoint getBindPoint() const = 0;

            [[nodiscard]] const PipelineLayout&   getLayout() const;
            [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout(const DescriptorSetIndex) const;

        protected:
            BasePipeline(const vk::Device, PipelineLayout&&, const vk::Pipeline);

        private:
            void destroy() noexcept;

        private:
            vk::Device     m_Device {nullptr};
            PipelineLayout m_Layout;
            vk::Pipeline   m_Handle {nullptr};
        };
    } // namespace rhi
} // namespace vultra