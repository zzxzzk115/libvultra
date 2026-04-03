#include "vultra/core/rhi/raytracing_pipeline.hpp"
#include "vultra/core/rhi/render_device.hpp"

namespace vultra
{
    namespace rhi
    {
        RayTracingPipeline::Builder::Builder()
        {
            m_ShaderStages.reserve(8);
            m_Groups.reserve(8);
        }

        RayTracingPipeline::Builder& RayTracingPipeline::Builder::setPipelineLayout(PipelineLayout pipelineLayout)
        {
            m_PipelineLayout = std::move(pipelineLayout);
            return *this;
        }

        RayTracingPipeline::Builder& RayTracingPipeline::Builder::setMaxRecursionDepth(uint32_t depth)
        {
            m_MaxRecursionDepth = depth;
            return *this;
        }

        RayTracingPipeline::Builder& RayTracingPipeline::Builder::addShader(const ShaderType       type,
                                                                            const ShaderStageInfo& stageInfo)
        {
            m_ShaderStages.push_back({type, stageInfo});
            return *this;
        }

        RayTracingPipeline::Builder& RayTracingPipeline::Builder::addBuiltinShader(const ShaderType type,
                                                                                   const SPIRV&     spv)
        {
            m_BuiltinShaderStages.push_back({type, spv});
            return *this;
        }

        RayTracingPipeline::Builder& RayTracingPipeline::Builder::addRaygenGroup(uint32_t shaderIndex)
        {
            const uint32_t        groupIndex = static_cast<uint32_t>(m_Groups.size());
            RaytracingShaderGroup group {};
            group.type          = RaytracingShaderGroup::Type::eGeneral;
            group.generalShader = shaderIndex;
            m_Groups.push_back(group);
            m_RaygenGroupIndices.push_back(groupIndex);
            return *this;
        }

        RayTracingPipeline::Builder& RayTracingPipeline::Builder::addMissGroup(uint32_t shaderIndex)
        {
            const uint32_t        groupIndex = static_cast<uint32_t>(m_Groups.size());
            RaytracingShaderGroup group {};
            group.type          = RaytracingShaderGroup::Type::eGeneral;
            group.generalShader = shaderIndex;
            m_Groups.push_back(group);
            m_MissGroupIndices.push_back(groupIndex);
            return *this;
        }

        RayTracingPipeline::Builder&
        RayTracingPipeline::Builder::addHitGroup(uint32_t                closestHitShader,
                                                 std::optional<uint32_t> anyHitShader,
                                                 std::optional<uint32_t> intersectionShader)
        {
            const uint32_t        groupIndex = static_cast<uint32_t>(m_Groups.size());
            RaytracingShaderGroup group {};
            group.type               = RaytracingShaderGroup::Type::eTrianglesHitGroup;
            group.generalShader      = UINT32_MAX;
            group.closestHitShader   = closestHitShader;
            group.anyHitShader       = anyHitShader.value_or(UINT32_MAX);
            group.intersectionShader = intersectionShader.value_or(UINT32_MAX);
            m_Groups.push_back(group);
            m_HitGroupIndices.push_back(groupIndex);
            return *this;
        }

        RayTracingPipeline::Builder& RayTracingPipeline::Builder::addCallableGroup(uint32_t shaderIndex)
        {
            const uint32_t        groupIndex = static_cast<uint32_t>(m_Groups.size());
            RaytracingShaderGroup group {};
            group.type          = RaytracingShaderGroup::Type::eGeneral;
            group.generalShader = shaderIndex;
            m_Groups.push_back(group);
            m_CallableGroupIndices.push_back(groupIndex);
            return *this;
        }

        RayTracingPipeline::RayTracingPipeline(PipelineLayout&&                     pipelineLayout,
                                               const std::uintptr_t                 handle,
                                               std::unique_ptr<IPipeline>           destroyBackend,
                                               std::vector<RaytracingShaderGroup>&& groups,
                                               std::vector<uint32_t>&&              raygenGroupIndices,
                                               std::vector<uint32_t>&&              missGroupIndices,
                                               std::vector<uint32_t>&&              hitGroupIndices,
                                               std::vector<uint32_t>&&              callableGroupIndices,
                                               std::unique_ptr<IRayTracingPipeline> backend) :
            BasePipeline {std::move(pipelineLayout), handle, std::move(destroyBackend)}, m_Groups(std::move(groups)),
            m_RaygenGroupIndices(std::move(raygenGroupIndices)), m_MissGroupIndices(std::move(missGroupIndices)),
            m_HitGroupIndices(std::move(hitGroupIndices)), m_CallableGroupIndices(std::move(callableGroupIndices)),
            m_Backend(std::move(backend))
        {}

        uint32_t RayTracingPipeline::getShaderGroupHandleSize() const
        {
            assert(m_Backend && m_Backend->isValid());
            return m_Backend->getProperties().shaderGroupHandleSize;
        }

        uint32_t RayTracingPipeline::getShaderGroupBaseAlignment() const
        {
            assert(m_Backend && m_Backend->isValid());
            return m_Backend->getProperties().shaderGroupBaseAlignment;
        }

        const Ref<ShaderBindingTable>& RayTracingPipeline::getSBT(rhi::RenderDevice& rd)
        {
            if (!m_SBT)
            {
                m_SBT = createRef<ShaderBindingTable>(std::move(rd.createShaderBindingTable(*this)));
            }
            return m_SBT;
        }
    } // namespace rhi
} // namespace vultra
