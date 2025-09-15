#include "vultra/function/renderer/builtin/framegraph_common.hpp"
#include "vultra/function/renderer/builtin/resources/camera_data.hpp"
#include "vultra/function/renderer/builtin/resources/frame_data.hpp"
#include "vultra/function/renderer/builtin/resources/light_data.hpp"

namespace vultra
{
    void read(FrameGraph::Builder& builder, const FrameData& data, const framegraph::PipelineStage pipelineStage)
    {
        builder.read(data.frameBlock,
                     framegraph::BindingInfo {.location = {.set = 0, .binding = 0}, .pipelineStage = pipelineStage});
    }

    void read(FrameGraph::Builder& builder, const CameraData& data, const framegraph::PipelineStage pipelineStage)
    {
        builder.read(data.cameraBlock,
                     framegraph::BindingInfo {.location = {.set = 1, .binding = 0}, .pipelineStage = pipelineStage});
    }

    void read(FrameGraph::Builder& builder, const LightData& data, const framegraph::PipelineStage pipelineStage)
    {
        builder.read(data.lightBlock,
                     framegraph::BindingInfo {.location = {.set = 1, .binding = 1}, .pipelineStage = pipelineStage});
    }
} // namespace vultra