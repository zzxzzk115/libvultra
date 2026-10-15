#pragma once

#include <vultra/assets/project_manifest.hpp>

namespace vultra
{
    struct ResearchMethod
    {
        std::string name;
        AssetId     graph;
    };

    struct ResearchProject
    {
        std::string                 name;
        uint32_t                    width    = 640;
        uint32_t                    height   = 480;
        uint64_t                    features = 0;
        std::vector<ResearchMethod> methods;
        AssetId                     comparison;
        std::string                 renderPath = "forward";
        std::string                 referenceMethod;
        std::string                 configurationLabel = "Configuration";
        std::string                 rendererSettings   = "{}"; // Reflected RenderSettings JSON.

        static std::optional<ResearchProject> read(const ProjectManifest& project);
        void                                  write(ProjectManifest& project) const;

    private:
        void validate(const ProjectManifest& project) const;
    };

} // namespace vultra
