#include "../common/ray_scene.hpp"
#include "../common/sample.hpp"

#include <vultra/core/rhi/shader_pipeline.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/asset/asset_options.hpp>
#include <vultra/function/camera/fps_camera.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <cstddef>

class RayQueryApp final : public vultra::ImGuiApp
{
public:
    explicit RayQueryApp(const argparse::ArgumentParser& cli) :
        ImGuiApp(
            {.title = "Vultra | Ray Query - Armadillo Shadows", .size = {1280, 800}, .features = VriFeature_RayQuery}),
        m_Options(sample::getOptions(cli)),
        m_Scene(getDevice(),
                vultra::importAsset("resources/models/raytracing_shadow/raytracing_shadow.gltf",
                                    vultra::getAssetImportOptions(cli))
                    .scene)
    {
        m_Shadows         = !cli.get<bool>("--no-shadows");
        m_ShowUi          = !cli.get<bool>("--no-ui");
        m_Camera.position = {0, 4, 8};
        m_Camera.pitch    = glm::radians(-20.0f);
        m_Camera.yaw      = 0;
        auto& device      = getDevice();
        try
        {
            const VriDescriptorRangeDesc range {0, 1, VriDescriptorType_AccelerationStructure, VriShaderStage_Fragment};
            const VriDescriptorSetDesc   set {0, &range, 1};
            const VriPushConstantDesc    push {0, sizeof(Parameters), VriShaderStage_Vertex | VriShaderStage_Fragment};
            const VriPipelineLayoutDesc  layout {&set, 1, &push, 1, push.shaderStages};
            vultra::check(device.core.CreatePipelineLayout(device.handle, &layout, &m_Layout),
                          "Create ray query layout");
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum         = 1;
            pool.accelerationStructureMaxNum = 1;
            vultra::check(device.core.CreateDescriptorPool(device.handle, &pool, &m_Pool), "Create ray query pool");
            vultra::check(device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_Set, 1), "Allocate ray query set");
            const VriDescriptor*               view = m_Scene.sceneView;
            const VriDescriptorRangeUpdateDesc update {&view, 1, 0};
            device.core.UpdateDescriptorRanges(m_Set, 0, 1, &update);
            m_Pipeline = std::make_unique<vultra::ShaderPipeline>(
                device,
                "examples/ray_query/shadows.slang",
                std::vector<vultra::ShaderEntry> {{"vertexMain", VriShaderStage_Vertex},
                                                  {"fragmentMain", VriShaderStage_Fragment}},
                [this](std::span<const VriShaderDesc> shaders)
                {
                    const VriVertexStreamDesc stream {sizeof(sample::RayVertex), 0, VriVertexStepRate_PerVertex};
                    VriVertexAttributeDesc    attributes[3] {};
                    attributes[0].format = VriFormat_RGBA32_SFLOAT;
                    attributes[0].offset = offsetof(sample::RayVertex, position);
                    attributes[1].format = VriFormat_RGBA32_SFLOAT;
                    attributes[1].offset = offsetof(sample::RayVertex, normal);
                    attributes[2].format = VriFormat_RGBA32_SFLOAT;
                    attributes[2].offset = offsetof(sample::RayVertex, color);
                    VriColorAttachmentDesc color {};
                    color.format         = getSwapchain().format();
                    color.colorWriteMask = VriColorWrite_RGBA;
                    VriGraphicsPipelineDesc desc {};
                    desc.pipelineLayout                  = m_Layout;
                    desc.shaders                         = shaders.data();
                    desc.shaderNum                       = uint32_t(shaders.size());
                    desc.vertexInput                     = {attributes, 3, &stream, 1};
                    desc.inputAssembly.topology          = VriPrimitiveTopology_TriangleList;
                    desc.rasterization.cullMode          = VriCullMode_None;
                    desc.rasterization.lineWidth         = 1;
                    desc.multisample.sampleNum           = 1;
                    desc.depthStencil.depthTest          = VRI_TRUE;
                    desc.depthStencil.depthWrite         = VRI_TRUE;
                    desc.depthStencil.depthCompareOp     = VriCompareOp_Less;
                    desc.outputMerger.colors             = &color;
                    desc.outputMerger.colorNum           = 1;
                    desc.outputMerger.depthStencilFormat = VriFormat_D32_SFLOAT;
                    VriPipeline* result                  = nullptr;
                    vultra::check(getDevice().core.CreateGraphicsPipeline(getDevice().handle, &desc, &result),
                                  "Create ray query pipeline");
                    return result;
                });
        }
        catch (...)
        {
            release();
            throw;
        }
    }

    ~RayQueryApp() override
    {
        getDevice().waitIdle();
        release();
    }

private:
    void release()
    {
        m_Pipeline.reset();
        if (m_Pool)
        {
            getDevice().core.DestroyDescriptorPool(m_Pool);
        }
        if (m_Layout)
        {
            getDevice().core.DestroyPipelineLayout(m_Layout);
        }
    }

    void onUpdate(float seconds) override
    {
        m_DeltaSeconds = seconds;
        m_Pipeline->poll();
    }

    void onPreRender() override
    {
        ImGuiApp::onPreRender();
        m_Camera.update(getWindow().input(), m_DeltaSeconds, getGui().inputCapture());
    }

    void onImGui() override
    {
        if (!m_ShowUi)
        {
            return;
        }
        ImGui::SetNextWindowSize({360, 190}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Ray query shadows");
        ImGui::TextUnformatted("WASD / QE: move; RMB: look; Shift: faster");
        ImGui::Checkbox("Ray traced shadows", &m_Shadows);
        ImGui::SliderFloat3("Light position", glm::value_ptr(m_Parameters.light), -10, 10);
        ImGui::SliderFloat("Ambient", &m_Parameters.options.x, 0, 1);
        if (!m_Pipeline->diagnostics().empty())
        {
            ImGui::TextWrapped("%s", m_Pipeline->diagnostics().c_str());
        }
        ImGui::End();
    }

    void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        auto&      device = getDevice();
        const auto size   = getSwapchain().size();
        if (!m_Depth || m_Depth->desc.width != size.width || m_Depth->desc.height != size.height)
        {
            m_Depth =
                std::make_unique<vultra::Texture>(device, vultra::depthTexture(size), nullptr, VriImageAspect_Depth);
        }
        const auto camera           = m_Camera.camera(size);
        m_Parameters.viewProjection = camera.projection * camera.view;
        m_Parameters.options.y      = m_Shadows ? 1.0f : 0.0f;
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        m_Depth->transition(cmd,
                            {VriAccess_DepthStencilAttachmentWrite,
                             VriLayout_DepthStencilAttachment,
                             VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
        VriAttachmentDesc color {};
        color.view             = target.view();
        color.loadOp           = VriAttachmentLoadOp_Clear;
        color.storeOp          = VriAttachmentStoreOp_Store;
        color.clearValue.color = {{0.2f, 0.3f, 0.3f, 1}};
        VriAttachmentDesc depth {};
        depth.view                          = m_Depth->view();
        depth.loadOp                        = VriAttachmentLoadOp_Clear;
        depth.storeOp                       = VriAttachmentStoreOp_Store;
        depth.clearValue.depthStencil.depth = 1;
        VriAttachmentsDesc attachments {};
        attachments.colors     = &color;
        attachments.colorNum   = 1;
        attachments.depth      = &depth;
        attachments.renderArea = {0, 0, size.width, size.height};
        attachments.layerNum   = 1;
        device.core.CmdBeginRendering(cmd, &attachments);
        const VriViewport viewport {0, 0, float(size.width), float(size.height), 0, 1};
        const VriRect     scissor {0, 0, size.width, size.height};
        device.core.CmdSetViewports(cmd, &viewport, 1);
        device.core.CmdSetScissors(cmd, &scissor, 1);
        device.core.CmdSetPipelineLayout(cmd, m_Layout);
        device.core.CmdSetPipeline(cmd, m_Pipeline->handle());
        device.core.CmdSetDescriptorSet(cmd, 0, m_Set);
        device.core.CmdSetConstants(cmd, 0, &m_Parameters, sizeof(Parameters));
        const VriVertexBufferBinding binding {m_Scene.vertices->handle, 0};
        device.core.CmdSetVertexBuffers(cmd, 0, &binding, 1);
        const VriDrawDesc draw {m_Scene.vertexCount, 1, 0, 0};
        device.core.CmdDraw(cmd, &draw);
        device.core.CmdEndRendering(cmd);
        drawGui(cmd, target);
    }

    void onPostRender(vultra::Texture& target) override
    {
        sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    struct Parameters
    {
        glm::mat4 viewProjection;
        glm::vec4 light {-5, 5, -5, 0};
        glm::vec4 options {0.1f, 1, 0, 0};
    } m_Parameters;

    static_assert(sizeof(Parameters) == 96);
    sample::Options                         m_Options;
    sample::RayScene                        m_Scene;
    vultra::FpsCamera                       m_Camera;
    float                                   m_DeltaSeconds = 0;
    bool                                    m_Shadows      = true;
    bool                                    m_ShowUi       = true;
    VriPipelineLayout*                      m_Layout       = nullptr;
    VriDescriptorPool*                      m_Pool         = nullptr;
    VriDescriptorSet*                       m_Set          = nullptr;
    std::unique_ptr<vultra::Texture>        m_Depth;
    std::unique_ptr<vultra::ShaderPipeline> m_Pipeline;
};

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("example-rayquery", "0.1.0", argparse::default_arguments::none);
    vultra::addAppOptions(cli);
    vultra::addAssetImportOptions(cli);
    sample::addCaptureOption(cli);
    cli.add_argument("--no-shadows").flag();
    cli.add_argument("--no-ui").flag();
    if (!vultra::parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    RayQueryApp app(cli);
    app.run(sample::getOptions(cli).frames);
    vultra::Logger::app().info("Ray query shadows: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
