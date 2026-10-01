#include <vultra/drivers/profiling/renderdoc_capture.hpp>

#include <renderdoc_app.h>

#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace vultra
{
    RenderDocCapture::RenderDocCapture()
    {
#if defined(_WIN32)
        m_Module     = GetModuleHandleA("renderdoc.dll");
        auto* symbol = m_Module ? GetProcAddress(static_cast<HMODULE>(m_Module), "RENDERDOC_GetAPI") : nullptr;
#elif defined(__linux__)
        m_Module     = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
        auto* symbol = m_Module ? dlsym(m_Module, "RENDERDOC_GetAPI") : nullptr;
#else
        void* symbol = nullptr;
#endif
        if (!symbol)
        {
#if defined(__linux__)
            if (m_Module)
            {
                dlclose(m_Module);
            }
#endif
            throw std::runtime_error("RenderDoc capture requested, but RenderDoc is not injected");
        }
        const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(symbol);
        if (getApi(eRENDERDOC_API_Version_1_1_2, &m_Api) != 1 || !m_Api)
        {
#if defined(__linux__)
            dlclose(m_Module);
#endif
            throw std::runtime_error("RenderDoc capture API 1.1.2 is unavailable");
        }
    }

    RenderDocCapture::~RenderDocCapture()
    {
        if (m_Active)
        {
            static_cast<RENDERDOC_API_1_1_2*>(m_Api)->EndFrameCapture(nullptr, nullptr);
        }
#if defined(__linux__)
        if (m_Module)
        {
            dlclose(m_Module);
        }
#endif
    }

    void RenderDocCapture::begin()
    {
        if (m_Active)
        {
            throw std::logic_error("RenderDoc capture already started");
        }
        static_cast<RENDERDOC_API_1_1_2*>(m_Api)->StartFrameCapture(nullptr, nullptr);
        m_Active = true;
    }

    void RenderDocCapture::end()
    {
        if (!m_Active)
        {
            throw std::logic_error("RenderDoc capture was not started");
        }
        const bool saved = static_cast<RENDERDOC_API_1_1_2*>(m_Api)->EndFrameCapture(nullptr, nullptr) == 1;
        m_Active         = false;
        if (!saved)
        {
            throw std::runtime_error("RenderDoc could not save the frame capture");
        }
    }
} // namespace vultra
