#include "vultra/core/rhi/raytracing/raytracing_pipeline.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_module.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] constexpr vk::ShaderStageFlagBits toVk(const ShaderType shaderType)
            {
                using enum ShaderType;
                switch (shaderType)
                {
                    case eRayGen:
                        return vk::ShaderStageFlagBits::eRaygenKHR;
                    case eMiss:
                        return vk::ShaderStageFlagBits::eMissKHR;
                    case eClosestHit:
                        return vk::ShaderStageFlagBits::eClosestHitKHR;
                    case eAnyHit:
                        return vk::ShaderStageFlagBits::eAnyHitKHR;
                    case eIntersect:
                        return vk::ShaderStageFlagBits::eIntersectionKHR;
                    default:
                        assert(false);
                        return {};
                }
            }
        } // namespace

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
            uint32_t groupIndex = static_cast<uint32_t>(m_Groups.size());
            m_Groups.push_back({.type = vk::RayTracingShaderGroupTypeKHR::eGeneral, .generalShader = shaderIndex});
            m_RaygenGroupIndices.push_back(groupIndex);
            return *this;
        }

        RayTracingPipeline::Builder& RayTracingPipeline::Builder::addMissGroup(uint32_t shaderIndex)
        {
            uint32_t groupIndex = static_cast<uint32_t>(m_Groups.size());
            m_Groups.push_back({.type = vk::RayTracingShaderGroupTypeKHR::eGeneral, .generalShader = shaderIndex});
            m_MissGroupIndices.push_back(groupIndex);
            return *this;
        }

        RayTracingPipeline::Builder&
        RayTracingPipeline::Builder::addHitGroup(uint32_t                closestHitShader,
                                                 std::optional<uint32_t> anyHitShader,
                                                 std::optional<uint32_t> intersectionShader)
        {
            uint32_t groupIndex = static_cast<uint32_t>(m_Groups.size());
            m_Groups.push_back({.type               = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
                                .generalShader      = VK_SHADER_UNUSED_KHR,
                                .closestHitShader   = closestHitShader,
                                .anyHitShader       = anyHitShader.value_or(VK_SHADER_UNUSED_KHR),
                                .intersectionShader = intersectionShader.value_or(VK_SHADER_UNUSED_KHR)});
            m_HitGroupIndices.push_back(groupIndex);
            return *this;
        }

        RayTracingPipeline::Builder& RayTracingPipeline::Builder::addCallableGroup(uint32_t shaderIndex)
        {
            uint32_t groupIndex = static_cast<uint32_t>(m_Groups.size());
            m_Groups.push_back({.type = vk::RayTracingShaderGroupTypeKHR::eGeneral, .generalShader = shaderIndex});
            m_CallableGroupIndices.push_back(groupIndex);
            return *this;
        }

        RayTracingPipeline RayTracingPipeline::Builder::build(RenderDevice& rd)
        {
            assert(!m_ShaderStages.empty() || !m_BuiltinShaderStages.empty());
            assert(!m_Groups.empty());

            std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groupInfos;
            groupInfos.reserve(m_Groups.size());
            for (const auto& g : m_Groups)
            {
                groupInfos.push_back(vk::RayTracingShaderGroupCreateInfoKHR {
                    g.type, g.generalShader, g.closestHitShader, g.anyHitShader, g.intersectionShader});
            }

            auto       reflection      = m_PipelineLayout ? std::nullopt : std::make_optional<ShaderReflection>();
            const auto numShaderStages = m_ShaderStages.size() + m_BuiltinShaderStages.size();
            assert(numShaderStages > 0);

            std::vector<ShaderModule> shaderModules;
            shaderModules.reserve(numShaderStages);
            std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
            shaderStages.reserve(numShaderStages);

            for (const auto& [shaderType, spv] : m_BuiltinShaderStages)
            {
                auto shaderModule =
                    rd.createShaderModule(spv, reflection ? std::addressof(reflection.value()) : nullptr);
                if (!shaderModule)
                    continue;

                vk::PipelineShaderStageCreateInfo shaderStageCreateInfo {};
                shaderStageCreateInfo.stage  = toVk(shaderType);
                shaderStageCreateInfo.module = vk::ShaderModule {shaderModule};
                shaderStageCreateInfo.pName  = "main";

                shaderStages.push_back(shaderStageCreateInfo);
                shaderModules.emplace_back(std::move(shaderModule));
            }

            for (const auto& [shaderType, shaderStageInfo] : m_ShaderStages)
            {
                auto shaderModule = rd.createShaderModule(shaderType,
                                                          shaderStageInfo.code,
                                                          shaderStageInfo.entryPointName,
                                                          shaderStageInfo.defines,
                                                          reflection ? std::addressof(reflection.value()) : nullptr);
                if (!shaderModule)
                    continue;

                vk::PipelineShaderStageCreateInfo shaderStageCreateInfo {};
                shaderStageCreateInfo.stage  = toVk(shaderType);
                shaderStageCreateInfo.module = vk::ShaderModule {shaderModule};
                shaderStageCreateInfo.pName  = shaderStageInfo.entryPointName.data();

                shaderStages.push_back(shaderStageCreateInfo);
                shaderModules.emplace_back(std::move(shaderModule));
            }
            if (shaderStages.size() != numShaderStages)
                return {};

            if (reflection.has_value())
                m_PipelineLayout = reflectPipelineLayout(rd, *reflection);
            assert(m_PipelineLayout);

            vk::RayTracingPipelineCreateInfoKHR pipelineInfo {};
            pipelineInfo.stageCount                   = static_cast<uint32_t>(shaderStages.size());
            pipelineInfo.pStages                      = shaderStages.data();
            pipelineInfo.groupCount                   = static_cast<uint32_t>(groupInfos.size());
            pipelineInfo.pGroups                      = groupInfos.data();
            pipelineInfo.maxPipelineRayRecursionDepth = m_MaxRecursionDepth;
            pipelineInfo.layout                       = m_PipelineLayout.getHandle();

            const auto device = rd.m_Device;

            auto result = device.createRayTracingPipelineKHR(nullptr, rd.m_PipelineCache, pipelineInfo);
            if (result.result != vk::Result::eSuccess)
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to create raytracing pipeline: {}",
                                  vk::to_string(result.result));
                throw std::runtime_error("Failed to create raytracing pipeline");
            }

            return RayTracingPipeline {rd.m_Device,
                                       std::move(m_PipelineLayout),
                                       result.value,
                                       std::move(m_Groups),
                                       std::move(m_RaygenGroupIndices),
                                       std::move(m_MissGroupIndices),
                                       std::move(m_HitGroupIndices),
                                       std::move(m_CallableGroupIndices),
                                       rd.m_RayTracingPipelineProperties};
        }

        RayTracingPipeline::RayTracingPipeline(const vk::Device                                  device,
                                               PipelineLayout&&                                  pipelineLayout,
                                               const vk::Pipeline                                handle,
                                               std::vector<RaytracingShaderGroup>&&              groups,
                                               std::vector<uint32_t>&&                           raygenGroupIndices,
                                               std::vector<uint32_t>&&                           missGroupIndices,
                                               std::vector<uint32_t>&&                           hitGroupIndices,
                                               std::vector<uint32_t>&&                           callableGroupIndices,
                                               vk::PhysicalDeviceRayTracingPipelinePropertiesKHR props) :
            BasePipeline {device, std::move(pipelineLayout), handle}, m_Groups(std::move(groups)), m_Props(props),
            m_RaygenGroupIndices(std::move(raygenGroupIndices)), m_MissGroupIndices(std::move(missGroupIndices)),
            m_HitGroupIndices(std::move(hitGroupIndices)), m_CallableGroupIndices(std::move(callableGroupIndices))
        {}

        uint32_t RayTracingPipeline::getShaderGroupHandleSize() const { return m_Props.shaderGroupHandleSize; }

        uint32_t RayTracingPipeline::getShaderGroupBaseAlignment() const { return m_Props.shaderGroupBaseAlignment; }

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