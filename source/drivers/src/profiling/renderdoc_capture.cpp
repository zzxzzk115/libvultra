#include <vultra/drivers/profiling/renderdoc_capture.hpp>

#include <renderdoc_app.h>

#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace vultra
{
    RenderDocCapture::RenderDocCapture(const std::filesystem::path& outputTemplate)
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
        if (!outputTemplate.empty())
        {
            const auto utf8 = outputTemplate.u8string();
            static_cast<RENDERDOC_API_1_1_2*>(m_Api)->SetCaptureFilePathTemplate(
                reinterpret_cast<const char*>(utf8.c_str()));
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
        m_File.clear();
        m_FirstCapture = static_cast<RENDERDOC_API_1_1_2*>(m_Api)->GetNumCaptures();
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
        auto*      api   = static_cast<RENDERDOC_API_1_1_2*>(m_Api);
        const auto count = api->GetNumCaptures();
        uint32_t   bytes = 0;
        if (count <= m_FirstCapture || !api->GetCapture(count - 1, nullptr, &bytes, nullptr) || bytes == 0)
        {
            throw std::runtime_error("RenderDoc saved no identifiable capture");
        }
        std::vector<char> name(bytes);
        if (!api->GetCapture(count - 1, name.data(), &bytes, nullptr))
        {
            throw std::runtime_error("Read RenderDoc capture path failed");
        }
        m_File = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name.data())));
    }

    const std::filesystem::path& RenderDocCapture::file() const
    {
        return m_File;
    }
} // namespace vultra
