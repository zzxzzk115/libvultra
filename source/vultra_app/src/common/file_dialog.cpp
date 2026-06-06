#include "common/file_dialog.hpp"
#include "common/ui_widgets.hpp"

#include <vultra/core/i18n/i18n.hpp>

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
                case FileDialogMode::File:
                    return ".*";
                case FileDialogMode::ProjectFile:
                    return ".vproject";
                case FileDialogMode::LuaScript:
                    return ".lua";
            }
            return nullptr;
        }

        ImGuiFileDialogFlags dialogFlags()
        {
            return ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_HideColumnType |
                   ImGuiFileDialogFlags_HideColumnSize | ImGuiFileDialogFlags_HideColumnDate |
                   ImGuiFileDialogFlags_DontShowHiddenFiles | ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering |
                   ImGuiFileDialogFlags_NaturalSorting | ImGuiFileDialogFlags_DisableThumbnailMode;
        }

        void configureFileDialogStyles()
        {
            static bool configured = false;
            if (configured)
                return;

            auto* dialog = ImGuiFileDialog::Instance();
            dialog->SetFileStyle(IGFD_FileStyleByTypeDir, nullptr, ImVec4 {0.36f, 0.68f, 1.00f, 1.0f});
            dialog->SetFileStyle(IGFD_FileStyleByExtention, ".lua", ImVec4 {0.52f, 0.82f, 1.00f, 1.0f});
            dialog->SetFileStyle(IGFD_FileStyleByExtention, ".vproject", ImVec4 {0.42f, 0.88f, 0.62f, 1.0f});
            configured = true;
        }

        std::filesystem::path currentDirectory()
        {
            std::error_code ec;
            auto            path = std::filesystem::current_path(ec);
            return ec ? std::filesystem::path {"."} : path.lexically_normal();
        }

        std::filesystem::path existingDialogDirectory(std::filesystem::path path)
        {
            std::error_code ec;
            if (path.empty())
                return currentDirectory();

            if (path.is_relative())
            {
                auto absolutePath = std::filesystem::absolute(path, ec);
                path              = ec ? path : absolutePath;
            }

            path = path.lexically_normal();
            if (std::filesystem::is_regular_file(path, ec) || path.extension() == ".vproject")
                path = path.parent_path();

            while (!path.empty() && !std::filesystem::is_directory(path, ec))
                path = path.parent_path();

            return path.empty() ? currentDirectory() : path.lexically_normal();
        }

        std::filesystem::path pathForDialogStart(const char* value, const std::filesystem::path& defaultPath)
        {
            if (value == nullptr || value[0] == '\0')
                return existingDialogDirectory(defaultPath);

            std::filesystem::path path {value};
            return existingDialogDirectory(path.empty() ? defaultPath : path);
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
        configureFileDialogStyles();
    }

    void FileDialogField::setDefaultPath(std::filesystem::path path)
    {
        m_DefaultPath = std::move(path);
    }

    bool FileDialogField::draw(const char* label, char* buffer, std::size_t bufferSize)
    {
        bool changed = false;
        changed |= ImGui::InputText(label, buffer, bufferSize);
        ImGui::SameLine();
        if (ImGui::Button((std::string(vultra::tr("fileDialog.browse")) + "##" + m_Key).c_str()))
            open(buffer);
        changed |= display(buffer, bufferSize);
        return changed;
    }

    bool FileDialogField::drawBrowseOnly(const char* label, char* buffer, std::size_t bufferSize)
    {
        bool changed = false;

        ImGui::PushID(m_Key.c_str());
        if (label && label[0] != '\0')
        {
            ImGui::TextUnformatted(label);
            ImGui::SameLine(160.0f);
        }

        const auto& style       = ImGui::GetStyle();
        const float buttonWidth = ImGui::CalcTextSize(vultra::tr("fileDialog.browse")).x + style.FramePadding.x * 2.0f;
        const float fieldWidth  = std::max(80.0f, ImGui::GetContentRegionAvail().x - buttonWidth - style.ItemSpacing.x);

        ImGui::SetNextItemWidth(fieldWidth);
        ImGui::InputText("##Value", buffer, bufferSize, ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::Button(vultra::tr("fileDialog.browse")))
            open(buffer);

        changed |= display(buffer, bufferSize);
        ImGui::PopID();
        return changed;
    }

    void FileDialogField::open(const char* currentValue)
    {
        IGFD::FileDialogConfig config;
        config.path  = pathForDialogStart(currentValue, m_DefaultPath).generic_string();
        config.flags = dialogFlags();
        ImGuiFileDialog::Instance()->OpenDialog(m_Key, m_Title, filtersFor(m_Mode), config);
    }

    bool FileDialogField::display(char* buffer, std::size_t bufferSize)
    {
        ScopedPopupStyle style;
        if (!ImGuiFileDialog::Instance()->Display(
                m_Key, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings, ImVec2(640.0f, 420.0f)))
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
