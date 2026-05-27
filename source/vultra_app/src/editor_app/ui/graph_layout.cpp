#include "editor_app/ui/graph_layout.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_set>

namespace vultra_app
{
    std::unordered_map<std::string, ImVec2>
    computeLayeredGraphLayout(const std::vector<GraphLayoutNode>& nodes,
                              const std::vector<GraphLayoutEdge>& edges,
                              const GraphLayoutConfig&            config)
    {
        std::unordered_map<std::string, size_t> index;
        index.reserve(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i)
            index.emplace(nodes[i].id, i);

        struct ResolvedEdge
        {
            size_t from {0};
            size_t to {0};
            int    fromOrder {0};
            int    toOrder {0};
        };

        std::vector<std::vector<ResolvedEdge>> outgoing(nodes.size());
        std::vector<std::vector<ResolvedEdge>> incoming(nodes.size());
        std::vector<int>                       indegree(nodes.size(), 0);
        for (const auto& edge : edges)
        {
            const auto from = index.find(edge.from);
            const auto to   = index.find(edge.to);
            if (from == index.end() || to == index.end() || from->second == to->second)
                continue;

            const ResolvedEdge resolved {
                .from = from->second,
                .to = to->second,
                .fromOrder = edge.fromOrder,
                .toOrder = edge.toOrder,
            };
            outgoing[from->second].push_back(resolved);
            incoming[to->second].push_back(resolved);
            ++indegree[to->second];
        }

        std::vector<int> layer(nodes.size(), 0);
        std::vector<size_t> ready;
        for (size_t i = 0; i < nodes.size(); ++i)
            if (indegree[i] == 0)
                ready.push_back(i);

        std::sort(ready.begin(), ready.end(), [&](const size_t a, const size_t b) {
            return nodes[a].order < nodes[b].order;
        });

        std::vector<size_t> topo;
        topo.reserve(nodes.size());
        while (!ready.empty())
        {
            const size_t current = ready.front();
            ready.erase(ready.begin());
            topo.push_back(current);

            for (const auto& edge : outgoing[current])
            {
                const size_t next = edge.to;
                layer[next] = std::max(layer[next], layer[current] + 1);
                if (--indegree[next] == 0)
                {
                    ready.push_back(next);
                    std::sort(ready.begin(), ready.end(), [&](const size_t a, const size_t b) {
                        return nodes[a].order < nodes[b].order;
                    });
                }
            }
        }

        if (topo.size() != nodes.size())
        {
            for (size_t i = 0; i < nodes.size(); ++i)
                layer[i] = nodes[i].order;
        }

        int maxLayer = 0;
        for (const int value : layer)
            maxLayer = std::max(maxLayer, value);
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].sink)
                layer[i] = maxLayer + 1;

        // Keep leaf parameter/source nodes close to the node they feed. This
        // prevents every constant from collapsing into the first column while
        // still keeping true multi-step chains flowing left to right.
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            if (!incoming[i].empty() || outgoing[i].empty())
                continue;

            int nearestConsumerLayer = std::numeric_limits<int>::max();
            for (const auto& edge : outgoing[i])
                nearestConsumerLayer = std::min(nearestConsumerLayer, layer[edge.to]);
            if (nearestConsumerLayer != std::numeric_limits<int>::max())
                layer[i] = std::max(0, nearestConsumerLayer - 1);
        }

        maxLayer = 0;
        for (const int value : layer)
            maxLayer = std::max(maxLayer, value);

        auto portOffset = [](const int order, const int count) {
            if (count <= 1)
                return 0.0f;
            return static_cast<float>(order) - static_cast<float>(count - 1) * 0.5f;
        };

        std::vector<float> lane(nodes.size(), 0.0f);
        for (size_t i = 0; i < nodes.size(); ++i)
            lane[i] = static_cast<float>(nodes[i].order);

        if (topo.size() == nodes.size())
        {
            for (auto it = topo.rbegin(); it != topo.rend(); ++it)
            {
                const size_t node = *it;
                if (!outgoing[node].empty())
                {
                    float sum = 0.0f;
                    for (const auto& edge : outgoing[node])
                        sum += lane[edge.to] + portOffset(edge.toOrder, nodes[edge.to].inputCount) -
                               portOffset(edge.fromOrder, nodes[node].outputCount);
                    lane[node] = sum / static_cast<float>(outgoing[node].size());
                }
                else if (!incoming[node].empty())
                {
                    float sum = 0.0f;
                    for (const auto& edge : incoming[node])
                        sum += portOffset(edge.toOrder, nodes[node].inputCount);
                    lane[node] = sum / static_cast<float>(incoming[node].size());
                }

                if (incoming[node].empty())
                    continue;

                for (const auto& edge : incoming[node])
                {
                    lane[edge.from] = lane[node] + portOffset(edge.toOrder, nodes[node].inputCount) -
                                      portOffset(edge.fromOrder, nodes[edge.from].outputCount);
                }
            }
        }

        std::unordered_map<int, std::vector<size_t>> layers;
        for (size_t i = 0; i < nodes.size(); ++i)
            layers[layer[i]].push_back(i);

        std::vector<int> layerKeys;
        layerKeys.reserve(layers.size());
        for (const auto& [layerIndex, _] : layers)
        {
            static_cast<void>(_);
            layerKeys.push_back(layerIndex);
        }
        std::sort(layerKeys.begin(), layerKeys.end());

        for (const int layerIndex : layerKeys)
        {
            auto& layerNodes = layers[layerIndex];
            std::sort(layerNodes.begin(), layerNodes.end(), [&](const size_t a, const size_t b) {
                if (lane[a] != lane[b])
                    return lane[a] < lane[b];
                return nodes[a].order < nodes[b].order;
            });
        }

        std::vector<float> adjustedLane = lane;
        for (const int layerIndex : layerKeys)
        {
            const auto& layerNodes = layers[layerIndex];
            float       previousBottom = -std::numeric_limits<float>::infinity();
            for (const size_t node : layerNodes)
            {
                adjustedLane[node] = std::max(adjustedLane[node], previousBottom + config.laneGap);
                previousBottom = adjustedLane[node] + std::max(nodes[node].heightLanes, 1.0f);
            }
        }

        float minLane = std::numeric_limits<float>::max();
        for (const float value : adjustedLane)
            minLane = std::min(minLane, value);

        std::unordered_map<std::string, ImVec2> result;
        result.reserve(nodes.size());
        for (const int layerIndex : layerKeys)
        {
            const auto& layerNodes = layers[layerIndex];
            const bool  sinkColumn = std::ranges::any_of(layerNodes, [&](const size_t node) { return nodes[node].sink; });
            const float x = config.origin.x + static_cast<float>(layerIndex) * config.columnSpacing +
                            (sinkColumn ? config.sinkExtraSpacing : 0.0f);
            for (const size_t node : layerNodes)
            {
                float displayLane = adjustedLane[node];
                if (nodes[node].sink && !incoming[node].empty())
                {
                    displayLane = std::numeric_limits<float>::max();
                    for (const auto& edge : incoming[node])
                    {
                        const float sinkLane = adjustedLane[edge.from] +
                                               portOffset(edge.fromOrder, nodes[edge.from].outputCount) -
                                               portOffset(edge.toOrder, nodes[node].inputCount);
                        displayLane = std::min(displayLane, sinkLane);
                    }
                }
                result[nodes[node].id] = ImVec2 {x, config.origin.y + (displayLane - minLane) * config.rowSpacing};
            }
        }
        return result;
    }
} // namespace vultra_app
