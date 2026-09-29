#include "../../common/colored_mesh.hpp"
#include "../../common/sample.hpp"

class TriangleApp final : public vultra::DesktopApp
{
public:
    explicit TriangleApp(const sample::Options& options) :
        vultra::DesktopApp(
            {.title = "Vultra | RHI - Indexed Triangle", .size = {1024, 768}, .swapchainFormat = VriFormat_BGRA8_SRGB}),
        m_Options(options),
        m_Triangle(getDevice(), getSwapchain().format(), sample::kTriangleVertices, sample::kTriangleIndices)
    {
    }

private:
    void onUpdate(float) override
    {
        m_Triangle.pipeline->poll();
    }

    void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        m_Triangle.draw(cmd, target, clear);
    }

    void onPostRender(vultra::Texture& target) override
    {
        sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    sample::Options     m_Options;
    sample::ColoredMesh m_Triangle;
};

int main(int argc, char** argv)
try
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return 0;
    }
    TriangleApp app(*options);
    app.run(options->frames);
    vultra::Logger::app().info("RHI triangle: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
