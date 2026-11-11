#include "../common/colored_mesh.hpp"
#include "../common/sample.hpp"
#include "../common/shader_pack.hpp"

#include <vultra/app/platform_app.hpp>

class TriangleApp final : public vultra::PlatformApp
{
public:
    static std::expected<std::unique_ptr<TriangleApp>, vultra::Error>
    create(Services services, const vultra::AppConfig& config, const sample::Options& options)
    {
        const auto program = sample::cookedShader(options.launch, "mesh.vshaderc");
        if (!program)
        {
            return std::unexpected(program.error());
        }
        auto triangle = sample::ColoredMesh::create(services.device(),
                                                    services.swapchain().format(),
                                                    sample::kTriangleVertices,
                                                    sample::kTriangleIndices,
                                                    VriPrimitiveTopology_TriangleList,
                                                    false,
                                                    *program ? &**program : nullptr);
        if (!triangle)
        {
            return std::unexpected(triangle.error());
        }
        return std::unique_ptr<TriangleApp>(
            new TriangleApp(std::move(services), config, options, std::move(*triangle)));
    }

private:
    TriangleApp(Services                             services,
                const vultra::AppConfig&             config,
                const sample::Options&               options,
                std::unique_ptr<sample::ColoredMesh> triangle) :
        vultra::PlatformApp(std::move(services), config),
        m_Options(options),
        m_Triangle(std::move(triangle))
    {
    }

    std::expected<void, vultra::Error> onUpdate(float) override
    {
        m_Triangle->pipeline->poll();
        return {};
    }

    std::expected<void, vultra::Error> onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        return m_Triangle->draw(cmd, target, clear);
    }

    std::expected<void, vultra::Error> onPostRender(vultra::Texture& target) override
    {
        return sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    sample::Options                      m_Options;
    std::unique_ptr<sample::ColoredMesh> m_Triangle;
};

int runVriTriangle(const vultra::AppLaunch& launch, int argc, char** argv)
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
    const vultra::AppConfig config {.title           = "Vultra | RHI - Indexed Triangle",
                                    .size            = {1024, 768},
                                    .swapchainFormat = VriFormat_BGRA8_SRGB};
    auto                    services = vultra::PlatformApp::createServices(config, launch);
    if (!services)
    {
        return sample::fail(services.error());
    }
    const auto app = TriangleApp::create(std::move(*services), config, **options);
    if (!app)
    {
        return sample::fail(app.error());
    }
    if (auto result = (*app)->run((*options)->frames); !result)
    {
        return sample::fail(result.error());
    }
    vultra::Logger::app().info("RHI triangle: {} frames", (*app)->frameCount());
    return 0;
}
