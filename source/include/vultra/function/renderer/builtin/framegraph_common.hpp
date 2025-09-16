#pragma once

#include "vultra/function/framegraph/framegraph_resource_access.hpp"

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace gfx
    {
        struct FrameData;
        void read(FrameGraph::Builder&,
                  const FrameData&,
                  const framegraph::PipelineStage = framegraph::PipelineStage::eVertexShader |
                                                    framegraph::PipelineStage::eFragmentShader);

        struct CameraData;
        void read(FrameGraph::Builder&,
                  const CameraData&,
                  const framegraph::PipelineStage = framegraph::PipelineStage::eVertexShader |
                                                    framegraph::PipelineStage::eFragmentShader |
                                                    framegraph::PipelineStage::eComputeShader);

        struct LightData;
        void read(FrameGraph::Builder&,
                  const LightData&,
                  const framegraph::PipelineStage = framegraph::PipelineStage::eVertexShader |
                                                    framegraph::PipelineStage::eFragmentShader);

        template<typename T>
        inline T& add(FrameGraphBlackboard& blackboard, const T& data)
        {
            if (blackboard.has<T>())
            {
                blackboard.get<T>() = data;
                return blackboard.get<T>();
            }
            else
            {
                blackboard.add<T>() = data;
                return blackboard.get<T>();
            }
        }
    } // namespace gfx
} // namespace vultra