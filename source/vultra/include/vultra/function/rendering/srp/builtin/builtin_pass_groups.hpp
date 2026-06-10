#pragma once

#include "vultra/function/rendering/srp/builtin/builtin_render_graph_pass.hpp"

#include <memory>
#include <vector>

namespace vultra
{
    // The builtin render-graph pass catalog, split by domain. Each TU appends its
    // adapters; makeBuiltinRenderGraphPasses() (builtin_render_graph_pass_factory.cpp)
    // composes the full catalog from these three groups.
    void appendSceneBuiltinRenderGraphPasses(std::vector<std::unique_ptr<IBuiltinRenderGraphPass>>& passes);
    void appendPostProcessBuiltinRenderGraphPasses(std::vector<std::unique_ptr<IBuiltinRenderGraphPass>>& passes);
    void appendGpuSceneBuiltinRenderGraphPasses(std::vector<std::unique_ptr<IBuiltinRenderGraphPass>>& passes);
} // namespace vultra
