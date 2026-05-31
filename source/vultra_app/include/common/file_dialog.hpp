#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace vultra_app::ui
{
    enum class FileDialogMode
    {
        Directory,
        File,
        ProjectFile,
        LuaScript,
    };

    class FileDialogField
    {
    public:
        FileDialogField(std::string key, std::string title, FileDialogMode mode);

        void setDefaultPath(std::filesystem::path path);

        bool draw(const char* label, char* buffer, std::size_t bufferSize);
        bool drawBrowseOnly(const char* label, char* buffer, std::size_t bufferSize);

    private:
        void open(const char* currentValue);
        bool display(char* buffer, std::size_t bufferSize);

        std::string           m_Key;
        std::string           m_Title;
        std::filesystem::path m_DefaultPath;
        FileDialogMode        m_Mode {FileDialogMode::Directory};
    };

} // namespace vultra_app::ui
