#include "project_templates.hpp"

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

        constexpr std::string_view kProjectSpecsReadme     = "# Project Specs\n";
        constexpr std::string_view kProjectTasksReadme     = "# Project Tasks\n";
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

    bool writeEmptyProjectScaffold(const std::filesystem::path& projectDir, std::string& errorMessage)
    {
        const auto resourcesDir = projectDir / "resources";
        return writeTextFile(resourcesDir / "scenes" / "main.vscn", kEmptyScene, errorMessage) &&
               writeProjectAiWorkspace(projectDir, errorMessage);
    }
} // namespace vultra_app
