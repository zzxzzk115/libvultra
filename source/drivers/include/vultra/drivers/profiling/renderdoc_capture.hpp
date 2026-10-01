#pragma once

namespace vultra
{
    // A frame capture is available only when RenderDoc has already injected its library.
    class RenderDocCapture
    {
    public:
        RenderDocCapture();
        ~RenderDocCapture();
        RenderDocCapture(const RenderDocCapture&)            = delete;
        RenderDocCapture& operator=(const RenderDocCapture&) = delete;
        void              begin();
        void              end();

    private:
        void* m_Module = nullptr;
        void* m_Api    = nullptr;
        bool  m_Active = false;
    };
} // namespace vultra
