#pragma once

#include "app_state.hpp"

#include <filesystem>
#include <string>

namespace vultra_app
{
    bool loadEditorSettings(const std::filesystem::path& path,
                            AppState::EditorSettings&   settings,
                            std::string*                error = nullptr);

    bool saveEditorSettings(const std::filesystem::path&        path,
                            const AppState::EditorSettings&    settings,
                            std::string*                       error = nullptr);
} // namespace vultra_app
