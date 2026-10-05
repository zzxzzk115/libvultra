#include <vultra/scene/scene_changes.hpp>

namespace vultra
{
    void SceneChanges::mark(SceneChange change)
    {
        switch (change)
        {
            case SceneChange::eStructure:
                ++structure;
                break;
            case SceneChange::eTransform:
                ++transforms;
                break;
            case SceneChange::eMaterial:
                ++materials;
                break;
            case SceneChange::eCamera:
                ++camera;
                break;
            case SceneChange::eLighting:
                ++lighting;
                break;
            case SceneChange::eEnvironment:
                ++environment;
                break;
            case SceneChange::eMetadata:
                ++metadata;
                break;
        }
    }
} // namespace vultra
