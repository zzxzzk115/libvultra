#include "common/file_dialog.hpp"

#include <ImGuiFileDialog/ImGuiFileDialog.h>
#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace vultra_app::ui
{
    namespace
    {
        const char* filtersFor(FileDialogMode mode)
        {
            switch (mode)
            {
            case FileDialogMode::Directory:
                return nullptr;
            case FileDialogMode::ProjectFile:
                return ".vproject";
            case FileDialogMode::LuaScript:
                return ".lua";
            }
            return nullptr;
        }

        std::filesystem::path pathForDialogStart(const char* value)
        {
            if (value == nullptr || value[0] == '\0')
                return ".";

            std::filesystem::path path {value};
            if (path.extension() == ".vproject")
                path = path.parent_path();
            return path.empty() ? std::filesystem::path {"."} : path;
        }

        void copyToBuffer(char* buffer, std::size_t bufferSize, const std::string& value)
        {
            if (buffer == nullptr || bufferSize == 0)
                return;

            const auto count = std::min(bufferSize - 1, value.size());
            std::memcpy(buffer, value.data(), count);
            buffer[count] = '\0';
        }
    } // namespace

    FileDialogField::FileDialogField(std::string key, std::string title, FileDialogMode mode) :
        m_Key(std::move(key)), m_Title(std::move(title)), m_Mode(mode)
    {
    }

    bool FileDialogField::draw(const char* label, char* buffer, std::size_t bufferSize)
    {
        bool changed = false;
        changed |= ImGui::InputText(label, buffer, bufferSize);
        ImGui::SameLine();
        if (ImGui::Button((std::string("Browse##") + m_Key).c_str()))
            open(buffer);
        changed |= display(buffer, bufferSize);
        return changed;
    }

    void FileDialogField::open(const char* currentValue)
    {
        IGFD::FileDialogConfig config;
        config.path  = pathForDialogStart(currentValue).generic_string();
        config.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_HideColumnType |
                       ImGuiFileDialogFlags_DontShowHiddenFiles;
        ImGuiFileDialog::Instance()->OpenDialog(m_Key, m_Title, filtersFor(m_Mode), config);
    }

    bool FileDialogField::display(char* buffer, std::size_t bufferSize)
    {
        if (!ImGuiFileDialog::Instance()->Display(m_Key, ImGuiWindowFlags_NoCollapse, ImVec2(560.0f, 360.0f)))
            return false;

        bool changed = false;
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            std::string selected;
            if (m_Mode == FileDialogMode::Directory)
                selected = ImGuiFileDialog::Instance()->GetFilePathName(IGFD_ResultMode_KeepInputFile);
            else
                selected = ImGuiFileDialog::Instance()->GetFilePathName(IGFD_ResultMode_KeepInputFile);

            if (selected.empty())
                selected = ImGuiFileDialog::Instance()->GetCurrentPath();
            copyToBuffer(buffer, bufferSize, std::filesystem::path(selected).lexically_normal().generic_string());
            changed = true;
        }

        ImGuiFileDialog::Instance()->Close();
        return changed;
    }
} // namespace vultra_app::ui
