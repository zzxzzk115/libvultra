#pragma once

#include <vultra/platform/input/input.hpp>

#include <memory>
#include <string>

namespace vultra::platform
{
    // Owns browser callback registrations; no callback may outlive this object or its Input.
    class WebInput
    {
    public:
        explicit WebInput(std::string canvas);
        ~WebInput();
        WebInput(const WebInput&)            = delete;
        WebInput& operator=(const WebInput&) = delete;
        Input&    input();
        bool      contextLost() const;
        bool      consumeVisibilityChange();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra::platform
