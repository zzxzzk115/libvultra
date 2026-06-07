#pragma once

#include "editor_app/ui/editor_window.hpp"
#include "editor_app/ui/windows/source_diagnostic_provider.hpp"

#include <ImGuiColorTextEdit/TextEditor.h>

#include <vultra/function/services/asset_service.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vultra_app
{
    class CodeEditorWindow final : public EditorWindow
    {
    public:
        CodeEditorWindow();

        void draw(EditorContext& ctx) override;

    private:
        void loadPath(EditorContext& ctx, const std::filesystem::path& path);
        void save(EditorContext& ctx);
        void reload(EditorContext& ctx);
        void reimport(EditorContext& ctx);
        void applyLanguageForPath(const std::filesystem::path& path);
        void refreshDiagnostics(EditorContext& ctx);
        void applyDiagnosticsToEditor();
        void drawDiagnosticsPanel();

        [[nodiscard]] bool hasOpenFile() const;
        [[nodiscard]] bool isDirty() const;

        std::filesystem::path            m_CurrentPath;
        std::filesystem::file_time_type  m_LoadedWriteTime {};
        TextEditor                       m_Editor;
        std::string                      m_LastSavedText;
        std::string                      m_Error;
        std::vector<vultra::AssetDiagnostic> m_Diagnostics;
        // Pluggable per-source-format diagnostic sources (import, render pass, ...).
        std::vector<std::unique_ptr<ISourceDiagnosticProvider>> m_DiagnosticProviders;
        bool                             m_Loaded {false};
        bool                             m_RequestFocus {false};
    };
} // namespace vultra_app
