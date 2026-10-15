#pragma once

#include <vultra/api/native_plugin.h>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/scene/node.hpp>

#include <filesystem>
#include <memory>
#include <string_view>

namespace vultra
{
    class EditorGui;
    class SceneTree;
    class ProjectManifest;

    class ScriptInstance : public NodeBehavior
    {
    public:
        virtual ~ScriptInstance()                                           = default;
        virtual void update(float deltaSeconds)                             = 0;
        virtual void gui(EditorGui& gui)                                    = 0;
        virtual void stop() noexcept                                        = 0;
        virtual void notify(VultraSceneEvent event, float deltaSeconds = 0) = 0;

        void enterTree() final
        {
            notify(VULTRA_SCENE_ENTER_TREE);
        }

        void ready() final
        {
            notify(VULTRA_SCENE_READY);
        }

        void physicsProcess(double deltaSeconds) final
        {
            notify(VULTRA_SCENE_PHYSICS_PROCESS, static_cast<float>(deltaSeconds));
        }

        void process(double deltaSeconds) final
        {
            update(static_cast<float>(deltaSeconds));
        }

        void exitTree() final
        {
            notify(VULTRA_SCENE_EXIT_TREE);
        }
    };

    std::unique_ptr<ScriptInstance>
    loadLuaScript(const std::filesystem::path& path, SceneTree& scene, ObjectId node, const ProjectManifest* project);
    std::unique_ptr<ScriptInstance> loadDotNetScript(const std::filesystem::path& path,
                                                     SceneTree&                   scene,
                                                     ObjectId                     node,
                                                     std::string_view             typeName,
                                                     const ScriptInstance*        previous = nullptr,
                                                     const ProjectManifest*       project  = nullptr,
                                                     const ScriptInstance*        peer     = nullptr);
} // namespace vultra
