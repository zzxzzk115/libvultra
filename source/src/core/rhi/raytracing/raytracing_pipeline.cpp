#include "vultra/core/rhi/raytracing/raytracing_pipeline.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_module.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"
#include "vultra/core/rhi/vk/macro.hpp"

namespace vultra
{
    namespace rhi
    {
        namespace
        {
            [[nodiscard]] constexpr vk::RayTracingShaderGroupTypeKHR toVk(const RaytracingShaderGroup::Type type)
            {
                switch (type)
                {
                    case RaytracingShaderGroup::Type::eGeneral:
                        return vk::RayTracingShaderGroupTypeKHR::eGeneral;
                    case RaytracingShaderGroup::Type::eTrianglesHitGroup:
                        return vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
                }
                return vk::RayTracingShaderGroupTypeKHR::eGeneral;
            }

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
            RaytracingShaderGroup group {};
            group.type          = RaytracingShaderGroup::Type::eGeneral;
            group.generalShader = shaderIndex;
            m_Groups.push_back(group);
            m_RaygenGroupIndices.push_back(groupIndex);
            return *this;
        }

        RayTracingPipeline::Builder& RayTracingPipeline::Builder::addMissGroup(uint32_t shaderIndex)
        {
            uint32_t groupIndex = static_cast<uint32_t>(m_Groups.size());
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
            uint32_t groupIndex = static_cast<uint32_t>(m_Groups.size());
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
            uint32_t groupIndex = static_cast<uint32_t>(m_Groups.size());
            RaytracingShaderGroup group {};
            group.type          = RaytracingShaderGroup::Type::eGeneral;
            group.generalShader = shaderIndex;
            m_Groups.push_back(group);
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
                    toVk(g.type), g.generalShader, g.closestHitShader, g.anyHitShader, g.intersectionShader});
            }

            auto       reflection      = m_PipelineLayout ? std::nullopt : std::make_optional<ShaderReflection>();
            const auto numShaderStages = m_ShaderStages.size() + m_BuiltinShaderStages.size();
            assert(numShaderStages > 0);

            std::vector<ShaderModule> shaderModules;
            shaderModules.reserve(numShaderStages);
            std::vector<vk::ShaderModule> shaderModuleHandles;
            shaderModuleHandles.reserve(numShaderStages);
            std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
            shaderStages.reserve(numShaderStages);

            for (const auto& [shaderType, spv] : m_BuiltinShaderStages)
            {
                auto shaderModule =
                    rd.createShaderModule(spv, reflection ? std::addressof(reflection.value()) : nullptr);
                if (!shaderModule)
                    continue;

                vk::ShaderModule shaderModuleHandle {nullptr};
                {
                    vk::ShaderModuleCreateInfo createInfo {};
                    createInfo.codeSize = sizeof(uint32_t) * shaderModule.getSpirv().size();
                    createInfo.pCode    = shaderModule.getSpirv().data();
                    const vk::Device device {reinterpret_cast<VkDevice>(rd.getNativeDeviceHandle())};
                    VK_CHECK(device.createShaderModule(&createInfo, nullptr, &shaderModuleHandle),
                             "RayTracingPipeline",
                             "Failed to create shader module");
                }

                vk::PipelineShaderStageCreateInfo shaderStageCreateInfo {};
                shaderStageCreateInfo.stage  = toVk(shaderType);
                shaderStageCreateInfo.module = shaderModuleHandle;
                shaderStageCreateInfo.pName  = "main";

                shaderStages.push_back(shaderStageCreateInfo);
                shaderModules.emplace_back(std::move(shaderModule));
                shaderModuleHandles.push_back(shaderModuleHandle);
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

                vk::ShaderModule shaderModuleHandle {nullptr};
                {
                    vk::ShaderModuleCreateInfo createInfo {};
                    createInfo.codeSize = sizeof(uint32_t) * shaderModule.getSpirv().size();
                    createInfo.pCode    = shaderModule.getSpirv().data();
                    const vk::Device device {reinterpret_cast<VkDevice>(rd.getNativeDeviceHandle())};
                    VK_CHECK(device.createShaderModule(&createInfo, nullptr, &shaderModuleHandle),
                             "RayTracingPipeline",
                             "Failed to create shader module");
                }

                vk::PipelineShaderStageCreateInfo shaderStageCreateInfo {};
                shaderStageCreateInfo.stage  = toVk(shaderType);
                shaderStageCreateInfo.module = shaderModuleHandle;
                shaderStageCreateInfo.pName  = shaderStageInfo.entryPointName.data();

                shaderStages.push_back(shaderStageCreateInfo);
                shaderModules.emplace_back(std::move(shaderModule));
                shaderModuleHandles.push_back(shaderModuleHandle);
            }
            if (shaderStages.size() != numShaderStages)
            {
                for (const auto shaderModuleHandle : shaderModuleHandles)
                {
                    const vk::Device device {reinterpret_cast<VkDevice>(rd.getNativeDeviceHandle())};
                    device.destroyShaderModule(shaderModuleHandle);
                }
                return {};
            }

            if (reflection.has_value())
                m_PipelineLayout = reflectPipelineLayout(rd, *reflection);
            assert(m_PipelineLayout);

            vk::RayTracingPipelineCreateInfoKHR pipelineInfo {};
            pipelineInfo.stageCount                   = static_cast<uint32_t>(shaderStages.size());
            pipelineInfo.pStages                      = shaderStages.data();
            pipelineInfo.groupCount                   = static_cast<uint32_t>(groupInfos.size());
            pipelineInfo.pGroups                      = groupInfos.data();
            pipelineInfo.maxPipelineRayRecursionDepth = m_MaxRecursionDepth;
            pipelineInfo.layout =
                vk::PipelineLayout {reinterpret_cast<VkPipelineLayout>(m_PipelineLayout.getHandle())};

            const vk::Device device {reinterpret_cast<VkDevice>(rd.getNativeDeviceHandle())};
            auto result = device.createRayTracingPipelineKHR(
                nullptr, vk::PipelineCache {reinterpret_cast<VkPipelineCache>(rd.getNativePipelineCacheHandle())}, pipelineInfo);
            for (const auto shaderModuleHandle : shaderModuleHandles)
            {
                device.destroyShaderModule(shaderModuleHandle);
            }
            if (result.result != vk::Result::eSuccess)
            {
                VULTRA_CORE_ERROR("[RenderDevice] Failed to create raytracing pipeline: {}",
                                  vk::to_string(result.result));
                throw std::runtime_error("Failed to create raytracing pipeline");
            }

            return RayTracingPipeline {rd.getNativeDeviceHandle(),
                                       std::move(m_PipelineLayout),
                                       reinterpret_cast<std::uintptr_t>(static_cast<VkPipeline>(result.value)),
                                       std::move(m_Groups),
                                       std::move(m_RaygenGroupIndices),
                                       std::move(m_MissGroupIndices),
                                       std::move(m_HitGroupIndices),
                                       std::move(m_CallableGroupIndices),
                                       rd.getRayTracingPipelineProperties()};
        }

        RayTracingPipeline::RayTracingPipeline(const std::uintptr_t                 device,
                                               PipelineLayout&&                     pipelineLayout,
                                               const std::uintptr_t                 handle,
                                               std::vector<RaytracingShaderGroup>&& groups,
                                               std::vector<uint32_t>&&              raygenGroupIndices,
                                               std::vector<uint32_t>&&              missGroupIndices,
                                               std::vector<uint32_t>&&              hitGroupIndices,
                                               std::vector<uint32_t>&&              callableGroupIndices,
                                               RayTracingPipelineProperties         props) :
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
