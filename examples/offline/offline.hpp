#pragma once

#include <vultra/assets/vpk_archive.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>

#include <filesystem>
#include <optional>

struct OfflineOptions
{
    uint64_t                               frames = 3;
    std::filesystem::path                  output;
    double                                 gain = 0.5;
    std::optional<vultra::GraphDefinition> graph;
};

// The same graph/draw code runs with desktop Slang sources or host-cooked VPK entries.
void runOfflineExample(const OfflineOptions& options, const vultra::VpkArchive* shaders = nullptr);
