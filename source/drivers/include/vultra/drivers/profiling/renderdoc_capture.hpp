#pragma once

#include <filesystem>

namespace vultra
{
    // A frame capture is available only when RenderDoc has already injected its library.
    class RenderDocCapture
    {
    public:
        explicit RenderDocCapture(const std::filesystem::path& outputTemplate = {});
        ~RenderDocCapture();
        RenderDocCapture(const RenderDocCapture&)                       = delete;
        RenderDocCapture&            operator=(const RenderDocCapture&) = delete;
        void                         begin();
        void                         end();
        const std::filesystem::path& file() const; // Saved capture from the last completed begin/end pair.

    private:
        void*                 m_Module       = nullptr;
        void*                 m_Api          = nullptr;
        bool                  m_Active       = false;
        uint32_t              m_FirstCapture = 0;
        std::filesystem::path m_File;
    };
} // namespace vultra
