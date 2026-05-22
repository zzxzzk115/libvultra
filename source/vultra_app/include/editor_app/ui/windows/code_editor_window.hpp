#pragma once

#include "editor_app/ui/editor_window.hpp"

#include <ImGuiColorTextEdit/TextEditor.h>

#include <filesystem>
#include <string>

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

        [[nodiscard]] bool hasOpenFile() const;
        [[nodiscard]] bool isDirty() const;

        std::filesystem::path            m_CurrentPath;
        std::filesystem::file_time_type  m_LoadedWriteTime {};
        TextEditor                       m_Editor;
        std::string                      m_LastSavedText;
        std::string                      m_Error;
        bool                             m_Loaded {false};
        bool                             m_RequestFocus {false};
    };
} // namespace vultra_app
