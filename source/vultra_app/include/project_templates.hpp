#pragma once

#include <filesystem>
#include <string>

namespace vultra_app
{
    // Scaffold the bare content for a brand-new empty project: an empty scene at
    // resources/scenes/main.vscn plus the ai/ collaboration workspace. The engine ships no project
    // *templates* of its own -- those come entirely from the remote template repository / catalog
    // (see editor_app/templates_repository.hpp). This is only the minimal bootstrap used to stand up
    // an empty project programmatically (e.g. the project.create_empty MCP command).
    bool writeEmptyProjectScaffold(const std::filesystem::path& projectDir, std::string& errorMessage);
} // namespace vultra_app
