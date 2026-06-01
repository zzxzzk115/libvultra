#include "project_templates.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

namespace vultra_app
{
    namespace
    {
        constexpr std::string_view kEmptyScene = R"([vscn]
version = 1
root = 0
)";

        constexpr std::string_view kMinimalScene = R"([vscn]
version = 1
root    = 0

[node id=1 name="Sun" parent=0 uuid="206733c1f880193cbd1dd2b65e0d0ca8"]
NameComponent/name = "Sun"
EntityStatusComponent/active = true
EntityStatusComponent/visible = true
EntityStatusComponent/locked = false
EntityStatusComponent/selectable = true
TransformComponent/position = (0, 4, 0)
TransformComponent/rotation = (-0.699544, -0.111872, 0.112315, 0.696784)
TransformComponent/scale = (1, 1, 1)
LightComponent/kind = 0
LightComponent/color = (1, 0.96, 0.9)
LightComponent/intensity = 8
LightComponent/range = 100
LightComponent/radius = 0.05
LightComponent/width = 1
LightComponent/height = 1
LightComponent/innerConeDegrees = 20
LightComponent/outerConeDegrees = 30
LightComponent/castsShadow = true
LightComponent/twoSided = false

[node id=2 name="Main Camera" parent=0 uuid="d6348e9e870dff93209ad02615cfefbb"]
NameComponent/name = "Main Camera"
EntityStatusComponent/active = true
EntityStatusComponent/visible = true
EntityStatusComponent/locked = false
EntityStatusComponent/selectable = true
TransformComponent/position = (0, 1.6, 4)
TransformComponent/rotation = (-0.130526, 0, 0, 0.991445)
TransformComponent/scale = (1, 1, 1)
CameraComponent/primary = true
CameraComponent/projection = 0
CameraComponent/fovYDegrees = 60
CameraComponent/orthographicHeight = 10
CameraComponent/nearClip = 0.01
CameraComponent/farClip = 1000
CameraComponent/clearMode = 1
CameraComponent/clearColor = (0.02, 0.025, 0.03, 1)
CameraComponent/rendererKey = "universal"

[node id=3 name="Environment" parent=0 uuid="8f7da3bd773fffb059ee2c3e5eb2f8c7"]
NameComponent/name = "Environment"
EntityStatusComponent/active = true
EntityStatusComponent/visible = true
EntityStatusComponent/locked = false
EntityStatusComponent/selectable = true
TransformComponent/position = (0, 0, 0)
TransformComponent/rotation = (0, 0, 0, 1)
TransformComponent/scale = (1, 1, 1)
EnvironmentComponent/active = true
EnvironmentComponent/skybox = "9a5cf6f3664c0e0b07179f44e84101cc"
EnvironmentComponent/ambientColor = (0.15, 0.15, 0.15)
EnvironmentComponent/ambientIntensity = 1
EnvironmentComponent/enableIBL = false
EnvironmentComponent/iblColor = (0.04, 0.045, 0.05)
EnvironmentComponent/iblIntensity = 1
)";

        constexpr std::string_view kProjectAiReadme = R"(# Vultra Project AI Workspace

This directory is the tracked AI collaboration layer for this Vultra game project.
Keep game-specific intent here, not in the engine repository Harness.

Before editing project content, read:

- `ai/game.md`
- relevant files under `ai/specs/`
- relevant files under `ai/tasks/`
- relevant files under `ai/knowledge/`

Project agents should use editor/runtime automation through Runtime MCP when
the editor is running, but should keep project intent, verification notes, and
generated-content assumptions in this `ai/` tree.
)";

        constexpr std::string_view kProjectGameBrief = R"(# Game Brief

## Identity

- Title: Untitled Vultra Project
- Genre:
- Camera / View:
- Target Platform:

## Technical Boundaries

- Default Scene: `res://scenes/main.vscn`
- Runtime Asset Root: `resources`

## Gameplay Scripting Assumptions

- Gameplay scripts use the repository-documented Lua API in
  `doc/lua_scripting.md`.
- If a desired mechanic needs an engine feature without a Lua binding, create an
  engine task to add the missing service/data behavior first, then expose a thin
  Lua binding.
- Record project-specific script patterns, input schemes, camera behavior, and
  physics assumptions under `ai/specs/` or `ai/knowledge/` before generating
  reusable content.
)";

        constexpr std::string_view kProjectSpecsReadme = "# Project Specs\n";
        constexpr std::string_view kProjectTasksReadme = "# Project Tasks\n";
        constexpr std::string_view kProjectWorkspaceReadme = "# Project Workspace\n";
        constexpr std::string_view kProjectKnowledgeReadme = R"(# Project Knowledge

Store stable game-specific facts here: controls, camera rules, art scale,
physics assumptions, script conventions, asset provenance, and verified
workflows.

Do not duplicate engine source knowledge unless the project depends on a
specific documented engine behavior.
)";
        constexpr std::string_view kProjectAgentsReadme = R"(# Project Agent Roles

Project agents operate on project content and project AI documents. They must
not edit Vultra engine source directly.

When project work reveals a missing engine scripting API, create or reference an
engine task. The engine task should add missing service/data support first,
then Lua bindings, then documentation.
)";
        constexpr std::string_view kProjectGeneratedReadme = "# Generated Content Index\n";

        std::string lowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        }

        bool writeTextFile(const std::filesystem::path& path, std::string_view text, std::string& errorMessage)
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                errorMessage =
                    "failed to create directory '" + path.parent_path().generic_string() + "': " + ec.message();
                return false;
            }

            std::ofstream file(path, std::ios::trunc);
            if (!file)
            {
                errorMessage = "failed to open '" + path.generic_string() + "' for writing";
                return false;
            }
            file << text;
            if (!file)
            {
                errorMessage = "failed to write '" + path.generic_string() + "'";
                return false;
            }
            return true;
        }

        bool copyFileIfPresent(const std::filesystem::path& source,
                               const std::filesystem::path& target,
                               std::string&                 errorMessage)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(source, ec))
            {
                errorMessage = "project template source file not found: " + source.generic_string();
                return false;
            }
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec)
            {
                errorMessage = "failed to create directory '" + target.parent_path().generic_string() + "': " + ec.message();
                return false;
            }
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
            {
                errorMessage = "failed to copy '" + source.generic_string() + "': " + ec.message();
                return false;
            }
            return true;
        }

        bool copyDirectoryIfPresent(const std::filesystem::path& source,
                                    const std::filesystem::path& target,
                                    std::string&                 errorMessage)
        {
            std::error_code ec;
            if (!std::filesystem::is_directory(source, ec))
            {
                errorMessage = "project template source directory not found: " + source.generic_string();
                return false;
            }
            std::filesystem::create_directories(target, ec);
            if (ec)
            {
                errorMessage = "failed to create directory '" + target.generic_string() + "': " + ec.message();
                return false;
            }
            std::filesystem::copy(source,
                                  target,
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::overwrite_existing,
                                  ec);
            if (ec)
            {
                errorMessage = "failed to copy '" + source.generic_string() + "': " + ec.message();
                return false;
            }
            return true;
        }

        bool writeMinimalProjectAssets(const std::filesystem::path& projectDir, std::string& errorMessage)
        {
            const auto sourceRoot = std::filesystem::current_path() / "resources";
            const auto resourcesDir = projectDir / "resources";

            return writeTextFile(resourcesDir / "scenes" / "main.vscn", kMinimalScene, errorMessage) &&
                   copyDirectoryIfPresent(sourceRoot / "render", resourcesDir / "render", errorMessage) &&
                   copyFileIfPresent(sourceRoot / "materials" / "default.vmatgraph.json",
                                     resourcesDir / "materials" / "default.vmatgraph.json",
                                     errorMessage) &&
                   copyFileIfPresent(sourceRoot / "shaders" / "project.vshaderlib.lua",
                                     resourcesDir / "shaders" / "project.vshaderlib.lua",
                                     errorMessage) &&
                   copyDirectoryIfPresent(sourceRoot / "shaders" / "fullscreen",
                                          resourcesDir / "shaders" / "fullscreen",
                                          errorMessage) &&
                   copyDirectoryIfPresent(sourceRoot / "shaders" / "compute",
                                          resourcesDir / "shaders" / "compute",
                                          errorMessage);
        }

        bool writeProjectAiWorkspace(const std::filesystem::path& projectDir, std::string& errorMessage)
        {
            return writeTextFile(projectDir / "ai" / "README.md", kProjectAiReadme, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "game.md", kProjectGameBrief, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "specs" / "README.md", kProjectSpecsReadme, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "tasks" / "README.md", kProjectTasksReadme, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "workspace" / "README.md", kProjectWorkspaceReadme, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "knowledge" / "README.md", kProjectKnowledgeReadme, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "agents" / "README.md", kProjectAgentsReadme, errorMessage) &&
                   writeTextFile(projectDir / "ai" / "generated" / "README.md", kProjectGeneratedReadme, errorMessage);
        }
    } // namespace

    ProjectTemplateKind projectTemplateKindFromString(std::string_view value)
    {
        auto text = lowerAscii(std::string(value));
        std::replace(text.begin(), text.end(), '-', '_');
        if (text == "empty")
            return ProjectTemplateKind::Empty;
        return ProjectTemplateKind::Minimal;
    }

    const char* projectTemplateKindName(ProjectTemplateKind kind)
    {
        switch (kind)
        {
            case ProjectTemplateKind::Empty:
                return "empty";
            case ProjectTemplateKind::Minimal:
                return "minimal";
        }
        return "minimal";
    }

    bool writeProjectTemplateAssets(const std::filesystem::path& projectDir,
                                    const ProjectTemplateKind     kind,
                                    std::string&                  errorMessage)
    {
        const auto resourcesDir = projectDir / "resources";
        switch (kind)
        {
            case ProjectTemplateKind::Empty:
                return writeTextFile(resourcesDir / "scenes" / "main.vscn", kEmptyScene, errorMessage) &&
                       writeProjectAiWorkspace(projectDir, errorMessage);
            case ProjectTemplateKind::Minimal:
                return writeMinimalProjectAssets(projectDir, errorMessage) &&
                       writeProjectAiWorkspace(projectDir, errorMessage);
        }
        return writeMinimalProjectAssets(projectDir, errorMessage) && writeProjectAiWorkspace(projectDir, errorMessage);
    }
} // namespace vultra_app
