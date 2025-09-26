#pragma once

#include "vultra/core/rhi/base_pipeline.hpp"
#include "vultra/core/rhi/shader_type.hpp"

#include <vector>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        struct RaytracingShaderGroup
        {
            // Vulkan shader group type
            vk::RayTracingShaderGroupTypeKHR type {vk::RayTracingShaderGroupTypeKHR::eGeneral};

            // Shader stage indices (relative to Builder's shader list)
            uint32_t generalShader {VK_SHADER_UNUSED_KHR};
            uint32_t closestHitShader {VK_SHADER_UNUSED_KHR};
            uint32_t anyHitShader {VK_SHADER_UNUSED_KHR};
            uint32_t intersectionShader {VK_SHADER_UNUSED_KHR};
        };

        class RaytracingPipeline final : public BasePipeline
        {
            friend class RenderDevice;

        public:
            RaytracingPipeline()                              = default;
            RaytracingPipeline(const RaytracingPipeline&)     = delete;
            RaytracingPipeline(RaytracingPipeline&&) noexcept = default;

            RaytracingPipeline& operator=(const RaytracingPipeline&)     = delete;
            RaytracingPipeline& operator=(RaytracingPipeline&&) noexcept = default;

            constexpr vk::PipelineBindPoint getBindPoint() const override
            {
                return vk::PipelineBindPoint::eRayTracingKHR;
            }

            const std::vector<RaytracingShaderGroup>& getShaderGroups() const { return m_Groups; }
            uint32_t getGroupCount() const { return static_cast<uint32_t>(m_Groups.size()); }

            uint32_t getShaderGroupHandleSize() const;
            uint32_t getShaderGroupBaseAlignment() const;

            uint32_t getRaygenGroupCount() const { return static_cast<uint32_t>(m_RaygenGroupIndices.size()); }
            uint32_t getMissGroupCount() const { return static_cast<uint32_t>(m_MissGroupIndices.size()); }
            uint32_t getHitGroupCount() const { return static_cast<uint32_t>(m_HitGroupIndices.size()); }
            uint32_t getCallableGroupCount() const { return static_cast<uint32_t>(m_CallableGroupIndices.size()); }

            class Builder
            {
            public:
                Builder();
                Builder(const Builder&)     = delete;
                Builder(Builder&&) noexcept = delete;
                ~Builder()                  = default;

                Builder& operator=(const Builder&)     = delete;
                Builder& operator=(Builder&&) noexcept = delete;

                Builder& setPipelineLayout(PipelineLayout);
                Builder& setMaxRecursionDepth(uint32_t depth);

                Builder& addShader(const ShaderType, const ShaderStageInfo&);
                Builder& addBuiltinShader(const ShaderType, const SPIRV&);

                Builder& addRaygenGroup(uint32_t shaderIndex);
                Builder& addMissGroup(uint32_t shaderIndex);
                Builder& addHitGroup(uint32_t                closestHitShader,
                                     std::optional<uint32_t> anyHitShader       = std::nullopt,
                                     std::optional<uint32_t> intersectionShader = std::nullopt);
                Builder& addCallableGroup(uint32_t shaderIndex);

                [[nodiscard]] RaytracingPipeline build(RenderDevice&);

            private:
                PipelineLayout m_PipelineLayout;
                uint32_t       m_MaxRecursionDepth {1};

                std::unordered_map<ShaderType, ShaderStageInfo> m_ShaderStages;
                std::unordered_map<ShaderType, SPIRV>           m_BuiltinShaderStages;
                std::vector<RaytracingShaderGroup>              m_Groups;

                std::vector<uint32_t> m_RaygenGroupIndices;
                std::vector<uint32_t> m_MissGroupIndices;
                std::vector<uint32_t> m_HitGroupIndices;
                std::vector<uint32_t> m_CallableGroupIndices;
            };

        private:
            RaytracingPipeline(const vk::Device                                  device,
                               PipelineLayout&&                                  pipelineLayout,
                               const vk::Pipeline                                handle,
                               std::vector<RaytracingShaderGroup>&&              groups,
                               std::vector<uint32_t>&&                           raygenGroupIndices,
                               std::vector<uint32_t>&&                           missGroupIndices,
                               std::vector<uint32_t>&&                           hitGroupIndices,
                               std::vector<uint32_t>&&                           callableGroupIndices,
                               vk::PhysicalDeviceRayTracingPipelinePropertiesKHR props);

        private:
            std::vector<RaytracingShaderGroup>                m_Groups;
            vk::PhysicalDeviceRayTracingPipelinePropertiesKHR m_Props;

            std::vector<uint32_t> m_RaygenGroupIndices;
            std::vector<uint32_t> m_MissGroupIndices;
            std::vector<uint32_t> m_HitGroupIndices;
            std::vector<uint32_t> m_CallableGroupIndices;
        };
    } // namespace rhi
} // namespace vultra