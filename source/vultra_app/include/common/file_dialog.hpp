#pragma once

#include <cstddef>
#include <string>

namespace vultra_app::ui
{
    enum class FileDialogMode
    {
        Directory,
        ProjectFile,
    };

    class FileDialogField
    {
    public:
        FileDialogField(std::string key, std::string title, FileDialogMode mode);

        bool draw(const char* label, char* buffer, std::size_t bufferSize);

    private:
        void open(const char* currentValue);
        bool display(char* buffer, std::size_t bufferSize);

        std::string    m_Key;
        std::string    m_Title;
        FileDialogMode m_Mode {FileDialogMode::Directory};
    };
} // namespace vultra_app::ui
