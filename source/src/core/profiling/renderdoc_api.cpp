#include "vultra/core/profiling/renderdoc_api.hpp"
#include "renderdoc_app.h"
#include "vultra/core/base/common_context.hpp"

#ifdef WIN32
#include <Windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace vultra
{
    RenderDocAPI::RenderDocAPI(bool enable)
    {
#ifdef VULTRA_ENABLE_RENDERDOC
        VULTRA_CORE_TRACE("[Profiling] Initializing RenderDoc API...");
        if (!enable)
        {
            VULTRA_CORE_TRACE("[Profiling] RenderDoc API is disabled by user.");
            return;
        }

        // Load the RenderDoc API
        m_IsAvailable = load();
        if (m_IsAvailable)
        {
            m_RenderDocAPI = getRenderDocAPI();
        }
#endif
    }

    RenderDocAPI::~RenderDocAPI()
    {
        if (m_RenderDocAPI)
        {
            m_RenderDocAPI->Shutdown();
            m_RenderDocAPI = nullptr;
        }
        unload();
    }

    bool RenderDocAPI::load()
    {
        if (m_IsAvailable)
        {
            VULTRA_CORE_TRACE("[Profiling] RenderDoc API is already loaded.");
            return true;
        }

        return loadDLL();
    }

    void RenderDocAPI::unload()
    {
        if (!m_IsAvailable)
        {
            return;
        }

        unloadDLL();

        m_IsAvailable    = false;
        m_RenderDocAPI   = nullptr;
        RENDERDOC_GetAPI = nullptr;

        VULTRA_CORE_TRACE("[Profiling] RenderDoc API unloaded successfully.");
    }

    // const RENDERDOC_API_1_6_0* RenderDocAPI::getAPI() const
    // {
    //     if (!m_IsAvailable)
    //     {
    //         VULTRA_CORE_ERROR("[Profiling] RenderDoc API is not available.");
    //         return nullptr;
    //     }
    //     return m_RenderDocAPI;
    // }

    bool RenderDocAPI::isAvailable() const { return m_IsAvailable; }

    bool RenderDocAPI::isFrameCapturing() const
    {
        if (m_RenderDocAPI)
        {
            return m_RenderDocAPI->IsFrameCapturing() != 0;
        }
        return false;
    }

    void RenderDocAPI::startFrameCapture() const
    {
        if (m_RenderDocAPI)
        {
            m_RenderDocAPI->StartFrameCapture(nullptr, nullptr);
        }
    }

    void RenderDocAPI::endFrameCapture() const
    {
        if (m_RenderDocAPI)
        {
            m_RenderDocAPI->EndFrameCapture(nullptr, nullptr);
        }
    }

    void RenderDocAPI::setCaptureFilePathTemplate(const std::string_view path) const
    {
        if (m_RenderDocAPI)
        {
            m_RenderDocAPI->SetCaptureFilePathTemplate(path.data());
        }
    }

    void RenderDocAPI::setCaptureTitle(const std::string_view title) const
    {
        if (m_RenderDocAPI)
        {
            m_RenderDocAPI->SetCaptureTitle(title.data());
        }
    }

    void RenderDocAPI::setCaptureFileComments(const std::string_view path, const std::string_view comments) const
    {
        if (m_RenderDocAPI)
        {
            m_RenderDocAPI->SetCaptureFileComments(path.data(), comments.data());
        }
    }

    bool RenderDocAPI::isTargetControlConnected() const
    {
        if (m_RenderDocAPI)
        {
            return m_RenderDocAPI->IsTargetControlConnected() != 0;
        }
        return false;
    }

    void RenderDocAPI::launchReplayUI(uint32_t connectTargetControl, const std::string_view cmdline) const
    {
        if (m_RenderDocAPI)
        {
            m_RenderDocAPI->LaunchReplayUI(connectTargetControl, cmdline.data());
        }
    }

    void RenderDocAPI::showReplayUI() const
    {
        if (m_RenderDocAPI)
        {
            m_RenderDocAPI->ShowReplayUI();
        }
    }

    uint32_t RenderDocAPI::getCaptureCount() const
    {
        if (m_RenderDocAPI)
        {
            return m_RenderDocAPI->GetNumCaptures();
        }
        return 0;
    }

    bool RenderDocAPI::loadDLL()
    {
        if (m_IsAvailable)
        {
            return true;
        }

#ifdef _WIN32
        if (m_Module = LoadLibrary("C:/Program Files/RenderDoc/renderdoc.dll"); m_Module)
        {
            RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(
                GetProcAddress(reinterpret_cast<HMODULE>(m_Module), "RENDERDOC_GetAPI"));
        }
        else
        {
            // Handle warning
            VULTRA_CORE_WARN(
                "[Profiling] Failed to load RenderDoc DLL. Ensure it is installed and the path is correct.");
            return false;
        }
#elif defined(__linux__)
        if (m_Module = dlopen("/usr/lib/librenderdoc.so", RTLD_NOW | RTLD_LOCAL); m_Module)
        {
            RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(m_Module, "RENDERDOC_GetAPI"));
        }
        else
        {
            // Handle warning
            VULTRA_CORE_WARN("[Profiling] Failed to load RenderDoc shared library. Ensure it is installed and the "
                             "path is correct.");
            return false;
        }
#else
#warning "Unsupported platform for RenderDoc API loading"
        return false;
#endif

        return true;
    }

    void RenderDocAPI::unloadDLL()
    {
        if (!m_IsAvailable)
        {
            return;
        }

#ifdef _WIN32
        FreeLibrary(reinterpret_cast<HMODULE>(m_Module));
#elif defined(__linux__) || defined(__android__)
        dlclose(m_Module);
#else
#warning "Unsupported platform for RenderDoc API loading"
#endif

        m_Module = nullptr;
    }

    RENDERDOC_API_1_6_0* RenderDocAPI::getRenderDocAPI()
    {
        if (!m_RenderDocAPI)
        {
            if (RENDERDOC_GetAPI)
            {
                int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&m_RenderDocAPI));
                assert(ret == 1);

                // Print the RenderDoc API version
                int major, minor, patch;
                m_RenderDocAPI->GetAPIVersion(&major, &minor, &patch);
                VULTRA_CORE_TRACE("[Profiling] RenderDoc API version: {}.{}.{}", major, minor, patch);

                // Set options for RenderDoc
#if _DEBUG
                m_RenderDocAPI->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 0);
                m_RenderDocAPI->SetCaptureOptionU32(eRENDERDOC_Option_APIValidation, 1);
#endif
                m_RenderDocAPI->SetCaptureFilePathTemplate("captures/myframe");
                m_RenderDocAPI->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
            }
        }
        return m_RenderDocAPI;
    }
} // namespace vultra