#pragma once

#include "vultra/core/base/scoped_enum_flags.hpp"

#include <vulkan/vulkan.hpp>

namespace vultra
{
    namespace rhi
    {
        // https://registry.khronos.org/vulkan/specs/1.3/html/chap7.html#synchronization-pipeline-stages
        enum class PipelineStages : VkPipelineStageFlags2
        {
            eNone = VK_PIPELINE_STAGE_2_NONE,

            eTop               = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            eVertexInput       = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
            eVertexShader      = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            eGeometryShader    = VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT,
            eFragmentShader    = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            eEarlyFragmentTest = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            eLateFragmentTest  = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            eFragmentTests = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            eColorAttachmentOutput = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            eComputeShader         = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            eTransfer              = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            eBlit                  = VK_PIPELINE_STAGE_2_BLIT_BIT,
            eBottom                = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,

            eAllTransfer = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            eAllGraphics = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
            eAllCommands = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };
    } // namespace rhi
} // namespace vultra

template<>
struct HasFlags<vultra::rhi::PipelineStages> : std::true_type
{};