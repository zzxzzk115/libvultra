#pragma once

#include <vultra/api/vultra_scene.generated.h>

namespace vultra
{
    class SceneTree;
    class ProjectManifest;

    // Owned by one plugin; its borrowed frame is active only during update().
    struct SceneAccess
    {
        SceneTree*             scene   = nullptr;
        uint64_t               serial  = 0;
        bool                   active  = false;
        const ProjectManifest* project = nullptr;
    };

    const VultraSceneApi& sceneApi();
} // namespace vultra
