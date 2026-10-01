#include "../common/sample.hpp"

class WindowApp final : public vultra::DesktopApp
{
public:
    explicit WindowApp(const sample::Options& options) :
        vultra::DesktopApp(
            {.title = "Vultra | Window - Clear", .size = {1024, 768}, .swapchainFormat = VriFormat_BGRA8_SRGB}),
        m_Options(options)
    {
    }

private:
    void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0.2f, 0.3f, 0.3f, 1};
        vultra::beginColorPass(getDevice(), cmd, target.view(), getSwapchain().size(), clear);
        getDevice().core.CmdEndRendering(cmd);
    }

    void onPostRender(vultra::Texture& target) override
    {
        sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    sample::Options m_Options;
};

int runWindow(int argc, char** argv)
try
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return 0;
    }
    WindowApp app(*options);
    app.run(options->frames);
    vultra::Logger::app().info("Window: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
