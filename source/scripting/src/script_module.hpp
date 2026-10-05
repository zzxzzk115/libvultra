#pragma once

#include <vultra/core/base/stable_id.hpp>

#include <filesystem>
#include <memory>
#include <string_view>

namespace vultra
{
    class EditorGui;
    class SceneTree;
    class ProjectManifest;

    class ScriptInstance
    {
    public:
        virtual ~ScriptInstance()               = default;
        virtual void update(float deltaSeconds) = 0;
        virtual void gui(EditorGui& gui)        = 0;
        virtual void stop() noexcept            = 0;
    };

    std::unique_ptr<ScriptInstance>
    loadLuaScript(const std::filesystem::path& path, SceneTree& scene, ObjectId node, const ProjectManifest* project);
    std::unique_ptr<ScriptInstance> loadDotNetScript(const std::filesystem::path& path,
                                                     SceneTree&                   scene,
                                                     ObjectId                     node,
                                                     std::string_view             typeName,
                                                     const ScriptInstance*        previous = nullptr,
                                                     const ProjectManifest*       project  = nullptr);
} // namespace vultra
