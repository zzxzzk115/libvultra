#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/rhi/base_pipeline.hpp"
#include "vultra/core/rhi/raytracing/shader_binding_table.hpp"
#include "vultra/core/rhi/raytracing/raytracing_pipeline_properties.hpp"
#include "vultra/core/rhi/structs/shader_type.hpp"

#include <vector>

namespace vultra
{
    namespace rhi
    {
        class RenderDevice;

        struct RaytracingShaderGroup
        {
            enum class Type
            {
                eGeneral,
                eTrianglesHitGroup,
            };

            Type type {Type::eGeneral};

            // Shader stage indices (relative to Builder's shader list)
            uint32_t generalShader {UINT32_MAX};
            uint32_t closestHitShader {UINT32_MAX};
            uint32_t anyHitShader {UINT32_MAX};
            uint32_t intersectionShader {UINT32_MAX};
        };

        class RayTracingPipeline final : public BasePipeline
        {
            friend class RenderDevice;

        public:
            RayTracingPipeline()                              = default;
            RayTracingPipeline(const RayTracingPipeline&)     = delete;
            RayTracingPipeline(RayTracingPipeline&&) noexcept = default;

            RayTracingPipeline& operator=(const RayTracingPipeline&)     = delete;
            RayTracingPipeline& operator=(RayTracingPipeline&&) noexcept = default;

            constexpr PipelineBindPoint getBindPoint() const override
            {
                return PipelineBindPoint::eRayTracing;
            }

            const std::vector<RaytracingShaderGroup>& getShaderGroups() const { return m_Groups; }
            uint32_t getGroupCount() const { return static_cast<uint32_t>(m_Groups.size()); }

            uint32_t getShaderGroupHandleSize() const;
            uint32_t getShaderGroupBaseAlignment() const;

            [[nodiscard]] const Ref<ShaderBindingTable>& getSBT(rhi::RenderDevice& rd);

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

                [[nodiscard]] RayTracingPipeline build(RenderDevice&);

            private:
                PipelineLayout m_PipelineLayout;
                uint32_t       m_MaxRecursionDepth {1};

                std::vector<std::pair<ShaderType, ShaderStageInfo>> m_ShaderStages;
                std::vector<std::pair<ShaderType, SPIRV>>           m_BuiltinShaderStages;
                std::vector<RaytracingShaderGroup>                  m_Groups;

                std::vector<uint32_t> m_RaygenGroupIndices;
                std::vector<uint32_t> m_MissGroupIndices;
                std::vector<uint32_t> m_HitGroupIndices;
                std::vector<uint32_t> m_CallableGroupIndices;
            };

        private:
            RayTracingPipeline(std::uintptr_t                                  device,
                               PipelineLayout&&                                  pipelineLayout,
                               std::uintptr_t                                    handle,
                               std::vector<RaytracingShaderGroup>&&              groups,
                               std::vector<uint32_t>&&                           raygenGroupIndices,
                               std::vector<uint32_t>&&                           missGroupIndices,
                               std::vector<uint32_t>&&                           hitGroupIndices,
                               std::vector<uint32_t>&&                           callableGroupIndices,
                               RayTracingPipelineProperties props);

        private:
            std::vector<RaytracingShaderGroup>                m_Groups;
            RayTracingPipelineProperties m_Props;

            std::vector<uint32_t> m_RaygenGroupIndices;
            std::vector<uint32_t> m_MissGroupIndices;
            std::vector<uint32_t> m_HitGroupIndices;
            std::vector<uint32_t> m_CallableGroupIndices;

            Ref<ShaderBindingTable> m_SBT {nullptr};
        };
    } // namespace rhi
} // namespace vultra

