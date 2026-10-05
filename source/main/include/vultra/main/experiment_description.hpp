#pragma once

#include <vultra/main/experiment_session.hpp>

namespace vultra
{
    // Version-1 input shared by the standalone batch tool and generated experiment API.
    // Cache locations are host-local; all other paths resolve relative to the description file.
    struct ExperimentDescription
    {
        ExperimentConfig               config;
        std::optional<GraphDefinition> graph;
        uint64_t                       frames   = 1;
        uint64_t                       warmup   = 0;
        float                          timeStep = 1.0f / 60.0f;

        static ExperimentDescription load(const std::filesystem::path& file);
        void                         save(const std::filesystem::path& file) const;
        void                         validate() const;
    };
} // namespace vultra
