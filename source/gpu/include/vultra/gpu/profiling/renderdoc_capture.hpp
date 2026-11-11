#pragma once

#include <vultra/core/base/error.hpp>

#include <expected>
#include <filesystem>
#include <memory>

namespace vultra
{
    // A frame capture is available only when RenderDoc has already injected its library.
    class RenderDocCapture
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<RenderDocCapture>, Error>
        create(const std::filesystem::path& outputTemplate = {});
        ~RenderDocCapture();
        RenderDocCapture(const RenderDocCapture&)                                   = delete;
        RenderDocCapture&                        operator=(const RenderDocCapture&) = delete;
        [[nodiscard]] std::expected<void, Error> begin();
        [[nodiscard]] std::expected<void, Error> end();
        const std::filesystem::path&             file() const; // Saved capture from the last completed begin/end pair.

    private:
        RenderDocCapture() = default;

        void*                 m_Module       = nullptr;
        void*                 m_Api          = nullptr;
        bool                  m_Active       = false;
        uint32_t              m_FirstCapture = 0;
        std::filesystem::path m_File;
    };
} // namespace vultra
