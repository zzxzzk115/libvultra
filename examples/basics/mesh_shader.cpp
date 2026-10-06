#include "../common/sample.hpp"

#include <vultra/drivers/rhi/shader_pipeline.hpp>

#include <vri/ext/vri_ext_meshshader.h>

class MeshTriangleApp final : public vultra::DesktopApp
{
public:
    explicit MeshTriangleApp(const sample::Options& options) :
        DesktopApp({.title           = "Vultra | Mesh Shading - Task + Mesh Triangle",
                    .size            = {1024, 768},
                    .swapchainFormat = VriFormat_BGRA8_SRGB,
                    .features        = VriFeature_MeshShader}),
        m_Options(options)
    {
        auto& device = getDevice();
        vultra::check(vriGetInterface(device.handle, VRI_INTERFACE_MESHSHADER, sizeof(m_Mesh), &m_Mesh),
                      "Get mesh shader interface");
        vultra::Logger::app().info("Mesh shading on {}", device.core.GetDeviceDesc(device.handle)->adapter.name);
        VriPipelineLayoutDesc layout {};
        layout.shaderStages = VriShaderStage_Task | VriShaderStage_Mesh | VriShaderStage_Fragment;
        vultra::check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout), "Create mesh layout");
        try
        {
            m_Pipeline = std::make_unique<vultra::ShaderPipeline>(
                device,
                "examples/basics/mesh_shader.slang",
                std::vector<vultra::ShaderEntry> {{"taskMain", VriShaderStage_Task},
                                                  {"meshMain", VriShaderStage_Mesh},
                                                  {"fragmentMain", VriShaderStage_Fragment}},
                [this](std::span<const VriShaderDesc> shaders)
                {
                    VriColorAttachmentDesc color {};
                    color.format         = getSwapchain().format();
                    color.colorWriteMask = VriColorWrite_RGBA;
                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineCache           = getDevice().pipelineCache;
                    desc.pipelineLayout          = m_Layout;
                    desc.shaders                 = shaders.data();
                    desc.shaderNum               = uint32_t(shaders.size());
                    desc.inputAssembly.topology  = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode  = VriCullMode_None;
                    desc.rasterization.lineWidth = 1;
                    desc.multisample.sampleNum   = 1;
                    desc.outputMerger.colors     = &color;
                    desc.outputMerger.colorNum   = 1;
                    VriPipeline* pipeline        = nullptr;
                    vultra::check(getDevice().core.CreateGraphicsPipeline(getDevice().handle, &desc, &pipeline),
                                  "Create task + mesh pipeline");
                    return pipeline;
                },
                "examples",
                std::vector<std::filesystem::path> {"examples/common"});
        }
        catch (...)
        {
            device.core.DestroyPipelineLayout(m_Layout);
            throw;
        }
    }

    ~MeshTriangleApp() override
    {
        getDevice().waitIdle();
        m_Pipeline.reset();
        getDevice().core.DestroyPipelineLayout(m_Layout);
    }

private:
    void onUpdate(float) override
    {
        m_Pipeline->poll();
    }

    void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        vultra::beginColorPass(getDevice(), cmd, target.view(), getSwapchain().size(), clear);
        getDevice().core.CmdSetPipelineLayout(cmd, m_Layout);
        getDevice().core.CmdSetPipeline(cmd, m_Pipeline->handle());
        m_Mesh.CmdDrawMeshTasks(cmd, 1, 1, 1);
        getDevice().core.CmdEndRendering(cmd);
    }

    void onPostRender(vultra::Texture& target) override
    {
        sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    sample::Options                         m_Options;
    VriMeshShaderInterface                  m_Mesh {};
    VriPipelineLayout*                      m_Layout = nullptr;
    std::unique_ptr<vultra::ShaderPipeline> m_Pipeline;
};

int runMeshShader(int argc, char** argv)
try
{
    const auto options = sample::readOptions(argc, argv);
    if (!options)
    {
        return 0;
    }
    MeshTriangleApp app(*options);
    app.run(options->frames);
    vultra::Logger::app().info("Task + mesh triangle: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
