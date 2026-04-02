#include "vultra/core/rhi/raytracing_pipeline.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/backends/vk/conversions.hpp"
#include "vultra/core/rhi/backends/vk/macro.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_pipeline_backend.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_render_device_access.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_raytracing_pipeline_backend.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/shader_module.hpp"
#include "vultra/core/rhi/shader_reflection.hpp"

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
                    const vk::Device device {reinterpret_cast<VkDevice>(VulkanRenderDeviceAccess::getDeviceHandle(rd))};
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
                    const vk::Device device {reinterpret_cast<VkDevice>(VulkanRenderDeviceAccess::getDeviceHandle(rd))};
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
                    const vk::Device device {reinterpret_cast<VkDevice>(VulkanRenderDeviceAccess::getDeviceHandle(rd))};
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

            const vk::Device device {reinterpret_cast<VkDevice>(VulkanRenderDeviceAccess::getDeviceHandle(rd))};
            auto result = device.createRayTracingPipelineKHR(
                nullptr,
                vk::PipelineCache {reinterpret_cast<VkPipelineCache>(VulkanRenderDeviceAccess::getPipelineCacheHandle(rd))},
                pipelineInfo);
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

            auto backend = std::make_unique<VulkanRayTracingPipelineBackend>(
                reinterpret_cast<std::uintptr_t>(static_cast<VkPipeline>(result.value)),
                rd.getRayTracingPipelineProperties());

            return RayTracingPipeline {std::move(m_PipelineLayout),
                                       backend->getHandle(),
                                       std::make_unique<VulkanPipelineBackend>(VulkanRenderDeviceAccess::getDeviceHandle(rd)),
                                       std::move(m_Groups),
                                       std::move(m_RaygenGroupIndices),
                                       std::move(m_MissGroupIndices),
                                       std::move(m_HitGroupIndices),
                                       std::move(m_CallableGroupIndices),
                                       std::move(backend)};
        }
    } // namespace rhi
} // namespace vultra
