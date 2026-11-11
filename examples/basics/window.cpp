#include "../common/sample.hpp"

#include <vultra/app/platform_app.hpp>

class WindowApp final : public vultra::PlatformApp
{
public:
    WindowApp(Services services, const vultra::AppConfig& config, const sample::Options& options) :
        vultra::PlatformApp(std::move(services), config),
        m_Options(options)
    {
    }

private:
    std::expected<void, vultra::Error> onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const auto view = target.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        const float clear[4] {0.2f, 0.3f, 0.3f, 1};
        vultra::beginColorPass(getDevice(), cmd, *view, getSwapchain().size(), clear);
        getDevice().core.CmdEndRendering(cmd);
        return {};
    }

    std::expected<void, vultra::Error> onPostRender(vultra::Texture& target) override
    {
        return sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    sample::Options m_Options;
};

int runWindow(const vultra::AppLaunch& launch, int argc, char** argv)
{
    const auto options = sample::readOptions(argc, argv, launch);
    if (!options)
    {
        return sample::fail(options.error());
    }
    if (!*options)
    {
        return 0;
    }
    const vultra::AppConfig config {.title           = "Vultra | Window - Clear",
                                    .size            = {1024, 768},
                                    .swapchainFormat = VriFormat_BGRA8_SRGB};
    auto                    services = vultra::PlatformApp::createServices(config, launch);
    if (!services)
    {
        return sample::fail(services.error());
    }
    WindowApp app(std::move(*services), config, **options);
    if (auto result = app.run((*options)->frames); !result)
    {
        return sample::fail(result.error());
    }
    vultra::Logger::app().info("Window: {} frames", app.frameCount());
    return 0;
}
