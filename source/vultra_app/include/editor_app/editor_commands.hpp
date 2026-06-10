#pragma once

#include "app_state.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace vultra_app
{
    inline void queueEditorCommand(AppState& state, AppState::EditorCommandType type, std::string payload)
    {
        if (payload.empty())
            return;
        state.pendingEditorCommands.push_back(AppState::EditorCommand {
            .type    = type,
            .payload = std::move(payload),
        });
    }

    inline void queueOpenScene(AppState& state, std::string uri)
    {
        queueEditorCommand(state, AppState::EditorCommandType::OpenScene, std::move(uri));
    }

    inline void queueOpenPrefab(AppState& state, std::string uri)
    {
        queueEditorCommand(state, AppState::EditorCommandType::OpenPrefab, std::move(uri));
    }

    inline void queueOpenRenderGraph(AppState& state, std::string uri)
    {
        queueEditorCommand(state, AppState::EditorCommandType::OpenRenderGraph, std::move(uri));
    }

    inline void queueOpenMaterialGraph(AppState& state, std::string uri)
    {
        queueEditorCommand(state, AppState::EditorCommandType::OpenMaterialGraph, std::move(uri));
    }

    inline void requestOpenCodeEditor(AppState& state, const std::filesystem::path& path)
    {
        state.codeEditorPath          = path.lexically_normal();
        state.codeEditorOpenRequested = true;
    }
} // namespace vultra_app
