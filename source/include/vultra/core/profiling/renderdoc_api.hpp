#pragma once

#include "vultra/core/os/window.hpp"
#include "vultra/core/rhi/render_device.hpp"

#include <renderdoc_app.h>

#include <string_view>

namespace vultra
{
    class RenderDocAPI
    {
    public:
        RenderDocAPI();
        ~RenderDocAPI();

        bool load();
        void unload();

        // const RENDERDOC_API_1_6_0* getAPI() const;
        bool isAvailable() const;

        bool isFrameCapturing() const;

        void startFrameCapture() const;
        void endFrameCapture() const;

        void setCaptureFilePathTemplate(const std::string_view path) const;
        void setCaptureTitle(const std::string_view title) const;
        void setCaptureFileComments(const std::string_view path, const std::string_view comments) const;

        bool isTargetControlConnected() const;
        void launchReplayUI(uint32_t connectTargetControl = 1, const std::string_view cmdline = "") const;
        void showReplayUI() const;

        uint32_t getCaptureCount() const;

    private:
        bool loadDLL();
        void unloadDLL();

        RENDERDOC_API_1_6_0* getRenderDocAPI();

    private:
        bool m_IsAvailable {false};

        void* m_Module {nullptr}; // Handle to the RenderDoc DLL

        RENDERDOC_API_1_6_0* m_RenderDocAPI {nullptr};

        // NOLINTBEGIN
        pRENDERDOC_GetAPI RENDERDOC_GetAPI {nullptr};
        // NOLINTEND
    };
} // namespace vultra