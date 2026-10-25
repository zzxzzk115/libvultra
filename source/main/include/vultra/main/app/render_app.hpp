#pragma once

#include <vultra/drivers/profiling/profiler.hpp>
#ifdef VULTRA_WITH_RENDERDOC
#include <vultra/drivers/profiling/renderdoc_capture.hpp>
#endif
#include <vultra/drivers/rhi/swapchain.hpp>
#include <vultra/main/app/app_config.hpp>
#include <vultra/main/app/base_app.hpp>

#include <memory>
#include <optional>
#include <string>

namespace vultra
{
    // Shared update/UI/record/submit/complete/present phases; hosts own event and surface lifetimes.
    class RenderApp : public BaseApp
    {
    protected:
        explicit RenderApp(const AppConfig& config);
        void                       resetFrames();
        void                       invalidateSurface();
        std::optional<std::string> advanceFrame(Device&    device,
                                                Swapchain& swapchain,
                                                Frame&     frame,
                                                Profiler&  profiler,
                                                float      deltaSeconds,
                                                bool       renderable    = true,
                                                Texture*   presentSource = nullptr);
        virtual void               onResize(Extent size);
        virtual void               onPreRender();
        virtual void               onRender(VriCommandBuffer* cmd, Texture& target) = 0;
        virtual void               onPostRender(Texture& target);
        virtual void               onPostPresent();
        virtual void               onFrameComplete(const FrameTiming& timing);
        virtual void               onRenderSkipped();

    private:
        Extent          m_PreviousSize {};
        FrameStatistics m_Statistics;
#ifdef VULTRA_WITH_RENDERDOC
        std::optional<uint64_t>           m_RenderDocFrame;
        std::unique_ptr<RenderDocCapture> m_RenderDoc;
#endif
    };
} // namespace vultra
