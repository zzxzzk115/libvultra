#pragma once

#include <vultra/gpu/profiling/profiler.hpp>
#ifdef VULTRA_WITH_RENDERDOC
#include <vultra/gpu/profiling/renderdoc_capture.hpp>
#endif
#include <vultra/app/app_config.hpp>
#include <vultra/app/base_app.hpp>
#include <vultra/gpu/swapchain.hpp>

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
        // Starts a run. Also attaches the RenderDoc capture requested by the configuration.
        std::expected<void, Error> resetFrames();
        void                       invalidateSurface();
        // Returns a new title suffix when the frame statistics publish one.
        std::expected<std::optional<std::string>, Error> advanceFrame(Device&    device,
                                                                      Swapchain& swapchain,
                                                                      Frame&     frame,
                                                                      Profiler&  profiler,
                                                                      float      deltaSeconds,
                                                                      bool       renderable    = true,
                                                                      Texture*   presentSource = nullptr);
        virtual std::expected<void, Error>               onResize(Extent size);
        virtual std::expected<void, Error>               onPreRender();
        virtual std::expected<void, Error>               onRender(VriCommandBuffer* cmd, Texture& target) = 0;
        virtual std::expected<void, Error>               onPostRender(Texture& target);
        virtual std::expected<void, Error>               onPostPresent();
        virtual std::expected<void, Error>               onFrameComplete(const FrameTiming& timing);
        virtual std::expected<void, Error>               onRenderSkipped();

    private:
        Extent                  m_PreviousSize {};
        FrameStatistics         m_Statistics;
        std::optional<uint64_t> m_RenderDocFrame;
#ifdef VULTRA_WITH_RENDERDOC
        std::unique_ptr<RenderDocCapture> m_RenderDoc;
#endif
    };
} // namespace vultra
