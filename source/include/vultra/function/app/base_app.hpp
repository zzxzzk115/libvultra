#pragma once

#include <cstdint>

namespace vultra
{
    // Platform-independent application contract. XR can supply its own frame loop.
    class BaseApp
    {
    public:
        BaseApp()                          = default;
        virtual ~BaseApp()                 = default;
        BaseApp(const BaseApp&)            = delete;
        BaseApp& operator=(const BaseApp&) = delete;

        virtual void run(uint64_t frameLimit = 0) = 0;

        void close()
        {
            m_CloseRequested = true;
        }

        uint64_t frameCount() const
        {
            return m_FrameCount;
        }

    protected:
        virtual void onPreUpdate(float deltaSeconds);
        virtual void onUpdate(float deltaSeconds);
        virtual void onPostUpdate(float deltaSeconds);

        bool     m_CloseRequested = false;
        uint64_t m_FrameCount     = 0;
    };
} // namespace vultra
