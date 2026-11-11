#pragma once

#include <vultra/core/base/error.hpp>

#include <cstdint>
#include <expected>

namespace vultra
{
    // Platform-independent application contract. XR can supply its own frame loop. Every hook returns an error
    // instead of throwing; run() stops at the first one and returns it.
    class BaseApp
    {
    public:
        BaseApp()                          = default;
        virtual ~BaseApp()                 = default;
        BaseApp(const BaseApp&)            = delete;
        BaseApp& operator=(const BaseApp&) = delete;

        [[nodiscard]] virtual std::expected<void, Error> run(uint64_t frameLimit = 0) = 0;

        void close()
        {
            m_CloseRequested = true;
        }

        uint64_t frameCount() const
        {
            return m_FrameCount;
        }

    protected:
        virtual std::expected<void, Error> onPreUpdate(float deltaSeconds);
        virtual std::expected<void, Error> onUpdate(float deltaSeconds);
        virtual std::expected<void, Error> onPostUpdate(float deltaSeconds);

        bool     m_CloseRequested = false;
        uint64_t m_FrameCount     = 0;
    };
} // namespace vultra
