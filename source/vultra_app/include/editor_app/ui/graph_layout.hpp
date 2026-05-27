#pragma once

#include <imgui.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vultra_app
{
    struct GraphLayoutNode
    {
        std::string id;
        int         order {0};
        int         inputCount {0};
        int         outputCount {0};
        float       heightLanes {1.0f};
        bool        sink {false};
    };

    struct GraphLayoutEdge
    {
        std::string from;
        std::string to;
        int         fromOrder {0};
        int         toOrder {0};
    };

    struct GraphLayoutConfig
    {
        ImVec2 origin {-720.0f, -160.0f};
        float  columnSpacing {300.0f};
        float  rowSpacing {130.0f};
        float  sinkExtraSpacing {140.0f};
        float  laneGap {0.15f};
    };

    [[nodiscard]] std::unordered_map<std::string, ImVec2>
    computeLayeredGraphLayout(const std::vector<GraphLayoutNode>& nodes,
                              const std::vector<GraphLayoutEdge>& edges,
                              const GraphLayoutConfig&            config = {});
} // namespace vultra_app
