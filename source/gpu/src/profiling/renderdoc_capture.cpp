#include <vultra/gpu/profiling/renderdoc_capture.hpp>

#include <renderdoc_app.h>

#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace vultra
{
    std::expected<std::unique_ptr<RenderDocCapture>, Error>
    RenderDocCapture::create(const std::filesystem::path& outputTemplate)
    {
        // ~RenderDocCapture closes the module handle on every failure below.
        std::unique_ptr<RenderDocCapture> capture(new RenderDocCapture());
#if defined(_WIN32)
        capture->m_Module = GetModuleHandleA("renderdoc.dll");
        auto* symbol =
            capture->m_Module ? GetProcAddress(static_cast<HMODULE>(capture->m_Module), "RENDERDOC_GetAPI") : nullptr;
#elif defined(__linux__)
        capture->m_Module = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
        auto* symbol      = capture->m_Module ? dlsym(capture->m_Module, "RENDERDOC_GetAPI") : nullptr;
#else
        void* symbol = nullptr;
#endif
        if (!symbol)
        {
            return makeError("RenderDoc capture requested, but RenderDoc is not injected");
        }
        const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(symbol);
        if (getApi(eRENDERDOC_API_Version_1_1_2, &capture->m_Api) != 1 || !capture->m_Api)
        {
            return makeError("RenderDoc capture API 1.1.2 is unavailable");
        }
        if (!outputTemplate.empty())
        {
            const auto utf8 = outputTemplate.u8string();
            static_cast<RENDERDOC_API_1_1_2*>(capture->m_Api)
                ->SetCaptureFilePathTemplate(reinterpret_cast<const char*>(utf8.c_str()));
        }
        return capture;
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

    std::expected<void, Error> RenderDocCapture::begin()
    {
        if (m_Active)
        {
            return makeError("RenderDoc capture already started");
        }
        m_File.clear();
        m_FirstCapture = static_cast<RENDERDOC_API_1_1_2*>(m_Api)->GetNumCaptures();
        static_cast<RENDERDOC_API_1_1_2*>(m_Api)->StartFrameCapture(nullptr, nullptr);
        m_Active = true;
        return {};
    }

    std::expected<void, Error> RenderDocCapture::end()
    {
        if (!m_Active)
        {
            return makeError("RenderDoc capture was not started");
        }
        const bool saved = static_cast<RENDERDOC_API_1_1_2*>(m_Api)->EndFrameCapture(nullptr, nullptr) == 1;
        m_Active         = false;
        if (!saved)
        {
            return makeError("RenderDoc could not save the frame capture");
        }
        auto*      api   = static_cast<RENDERDOC_API_1_1_2*>(m_Api);
        const auto count = api->GetNumCaptures();
        uint32_t   bytes = 0;
        if (count <= m_FirstCapture || !api->GetCapture(count - 1, nullptr, &bytes, nullptr) || bytes == 0)
        {
            return makeError("RenderDoc saved no identifiable capture");
        }
        std::vector<char> name(bytes);
        if (!api->GetCapture(count - 1, name.data(), &bytes, nullptr))
        {
            return makeError("Read RenderDoc capture path failed");
        }
        m_File = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name.data())));
        return {};
    }

    const std::filesystem::path& RenderDocCapture::file() const
    {
        return m_File;
    }
} // namespace vultra
