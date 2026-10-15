#pragma once

#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <span>
#include <string>
#include <string_view>

namespace vultra
{
    struct GraphCapture
    {
        std::string_view      port;
        std::string_view      file;
        RenderGraph::Resource resource;
    };

    // Completed frame only. Associates files, producer passes, inclusive timing events and physical allocations.
    // Memory is VRI allocator data; untracked imports/backends stay explicitly unknown, not estimated.
    std::string graphReport(const RenderGraph&            graph,
                            std::span<const PassTiming>   timings,
                            std::span<const GraphCapture> images     = {},
                            std::string_view              gpuCapture = {});
} // namespace vultra
