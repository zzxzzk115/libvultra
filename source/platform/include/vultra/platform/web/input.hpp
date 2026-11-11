#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/platform/input/input.hpp>

#include <expected>
#include <memory>
#include <string>

namespace vultra::platform
{
    // Owns browser callback registrations; no callback may outlive this object or its Input.
    class WebInput
    {
    public:
        [[nodiscard]] static std::expected<WebInput, Error> create(std::string canvas);
        ~WebInput();
        WebInput(WebInput&&) noexcept;
        WebInput& operator=(WebInput&&) noexcept;
        WebInput(const WebInput&)            = delete;
        WebInput& operator=(const WebInput&) = delete;
        Input&    input();
        bool      contextLost() const;
        bool      consumeVisibilityChange();

    private:
        struct Impl;
        explicit WebInput(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra::platform
