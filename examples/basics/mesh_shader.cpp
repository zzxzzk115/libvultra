#include "../common/sample.hpp"

#include <vultra/app/desktop_app.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <vri/ext/vri_ext_meshshader.h>

class MeshTriangleApp final : public vultra::DesktopApp
{
public:
    static std::expected<std::unique_ptr<MeshTriangleApp>, vultra::Error>
    create(Services services, const vultra::AppConfig& config, const sample::Options& options)
    {
        auto&                  device = services.device();
        VriMeshShaderInterface mesh {};
        if (auto found = vultra::check(vriGetInterface(device.handle, VRI_INTERFACE_MESHSHADER, sizeof(mesh), &mesh),
                                       "Get mesh shader interface");
            !found)
        {
            return std::unexpected(found.error());
        }
        vultra::Logger::app().info("Mesh shading on {}", device.core.GetDeviceDesc(device.handle)->adapter.name);
        VriPipelineLayoutDesc layoutDesc {};
        layoutDesc.shaderStages   = VriShaderStage_Task | VriShaderStage_Mesh | VriShaderStage_Fragment;
        VriPipelineLayout* layout = nullptr;
        if (auto created = vultra::check(device.core.CreatePipelineLayout(device.handle, &layoutDesc, &layout),
                                         "Create mesh layout");
            !created)
        {
            return std::unexpected(created.error());
        }
        // The device lives in the services at a stable address, so the builder may keep a reference for reloads.
        auto pipeline = vultra::ShaderPipeline::create(
            device,
            "examples/basics/mesh_shader.slang",
            std::vector<vultra::ShaderEntry> {{"taskMain", VriShaderStage_Task},
                                              {"meshMain", VriShaderStage_Mesh},
                                              {"fragmentMain", VriShaderStage_Fragment}},
            [&device, format = services.swapchain().format(), layout](
                std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, vultra::Error>
            {
                VriColorAttachmentDesc color {};
                color.format         = format;
                color.colorWriteMask = VriColorWrite_RGBA;
                VriGraphicsPipelineDesc desc {};
                desc.pipelineCache           = device.pipelineCache;
                desc.pipelineLayout          = layout;
                desc.shaders                 = shaders.data();
                desc.shaderNum               = uint32_t(shaders.size());
                desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                desc.rasterization.cullMode  = VriCullMode_None;
                desc.rasterization.lineWidth = 1;
                desc.multisample.sampleNum   = 1;
                desc.outputMerger.colors     = &color;
                desc.outputMerger.colorNum   = 1;
                VriPipeline* pipeline        = nullptr;
                if (auto created = vultra::check(device.core.CreateGraphicsPipeline(device.handle, &desc, &pipeline),
                                                 "Create task + mesh pipeline");
                    !created)
                {
                    return std::unexpected(created.error());
                }
                return pipeline;
            },
            "examples",
            std::vector<std::filesystem::path> {"examples/common"});
        if (!pipeline)
        {
            device.core.DestroyPipelineLayout(layout);
            return std::unexpected(pipeline.error());
        }
        return std::unique_ptr<MeshTriangleApp>(
            new MeshTriangleApp(std::move(services), config, options, mesh, layout, std::move(*pipeline)));
    }

    ~MeshTriangleApp() override
    {
        getDevice().waitIdleForDestruction();
        m_Pipeline.reset();
        getDevice().core.DestroyPipelineLayout(m_Layout);
    }

private:
    MeshTriangleApp(Services                                services,
                    const vultra::AppConfig&                config,
                    const sample::Options&                  options,
                    const VriMeshShaderInterface&           mesh,
                    VriPipelineLayout*                      layout,
                    std::unique_ptr<vultra::ShaderPipeline> pipeline) :
        DesktopApp(std::move(services), config),
        m_Options(options),
        m_Mesh(mesh),
        m_Layout(layout),
        m_Pipeline(std::move(pipeline))
    {
    }

    std::expected<void, vultra::Error> onUpdate(float) override
    {
        m_Pipeline->poll();
        return {};
    }

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
        const float clear[4] {0, 0, 0, 1};
        vultra::beginColorPass(getDevice(), cmd, *view, getSwapchain().size(), clear);
        getDevice().core.CmdSetPipelineLayout(cmd, m_Layout);
        getDevice().core.CmdSetPipeline(cmd, m_Pipeline->handle());
        m_Mesh.CmdDrawMeshTasks(cmd, 1, 1, 1);
        getDevice().core.CmdEndRendering(cmd);
        return {};
    }

    std::expected<void, vultra::Error> onPostRender(vultra::Texture& target) override
    {
        return sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    sample::Options                         m_Options;
    VriMeshShaderInterface                  m_Mesh {};
    VriPipelineLayout*                      m_Layout = nullptr;
    std::unique_ptr<vultra::ShaderPipeline> m_Pipeline;
};

int runMeshShader(const vultra::AppLaunch& launch, int argc, char** argv)
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return sample::fail(options.error());
    }
    if (!*options)
    {
        return 0;
    }
    const vultra::AppConfig config {.title           = "Vultra | Mesh Shading - Task + Mesh Triangle",
                                    .size            = {1024, 768},
                                    .swapchainFormat = VriFormat_BGRA8_SRGB,
                                    .features        = VriFeature_MeshShader};
    auto                    services = vultra::DesktopApp::createServices(config, launch);
    if (!services)
    {
        return sample::fail(services.error());
    }
    const auto app = MeshTriangleApp::create(std::move(*services), config, **options);
    if (!app)
    {
        return sample::fail(app.error());
    }
    if (auto result = (*app)->run((*options)->frames); !result)
    {
        return sample::fail(result.error());
    }
    vultra::Logger::app().info("Task + mesh triangle: {} frames", (*app)->frameCount());
    return 0;
}
