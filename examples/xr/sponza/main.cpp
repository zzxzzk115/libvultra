#include "../../common/xr_sample.hpp"

#include <vultra/function/asset/asset_options.hpp>
#include <vultra/function/camera/fps_camera.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>

#include <glm/gtc/quaternion.hpp>

class XrSponzaApp final : public sample::XrSample
{
public:
    explicit XrSponzaApp(const argparse::ArgumentParser& cli) :
        XrSample(sample::getOptions(cli), "Vultra | OpenXR - Sponza and Mirror"),
        m_Environment(device(), "resources/textures/environment_maps/citrus_orchard_puresky_1k.hdr"),
        m_Scene(device(),
                vultra::importAsset("resources/models/Sponza/Sponza.gltf", vultra::getAssetImportOptions(cli))),
        m_EyeBlit(device(), eyeFormat(), 2)
    {
        m_Rig.position.y = 0; // Eye height comes from the tracked pose.
        for (auto& eye : m_Eyes)
        {
            eye.renderer =
                std::make_unique<vultra::BuiltinRenderer>(device(), m_Scene, m_Environment, VriFormat_RGBA16_SFLOAT);
        }
    }

private:
    void onUpdate(float seconds) override
    {
        m_DeltaSeconds = seconds;
        for (auto& eye : m_Eyes)
        {
            eye.renderer->pollShaders();
        }
    }

    void onImGui() override
    {
        ImGui::SetNextWindowSize({350, 190}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Sponza");
        ImGui::TextUnformatted("WASD / QE: move rig; RMB: turn; head pose remains tracked");
        auto& settings = m_Eyes[0].renderer->settings;
        ImGui::Checkbox("IBL", &settings.ibl);
        ImGui::SliderFloat("Exposure", &settings.exposure, -4, 4);
        int shadows = int(settings.shadowFilter);
        ImGui::Combo("Shadows", &shadows, "Off\0Hard\0PCF\0PCSS\0");
        settings.shadowFilter        = vultra::ShadowFilter(shadows);
        m_Eyes[1].renderer->settings = settings;
        ImGui::End();
    }

    void onPreRender() override
    {
        m_Rig.update(window().input(), m_DeltaSeconds, gui().inputCapture());
    }

    void onRenderEye(VriCommandBuffer* cmd, const vultra::XREye& tracked, uint32_t index) override
    {
        auto&                eye = m_Eyes[index];
        const vultra::Extent size {tracked.color->desc.width, tracked.color->desc.height};
        if (!eye.graph || eye.size != size)
        {
            eye.graph   = std::make_unique<vultra::RenderGraph>(device());
            eye.outputs = eye.renderer->addPasses(*eye.graph, size);
            eye.graph->exportResource(eye.outputs.color);
            eye.graph->compile();
            eye.size = size;
            m_EyeBlit.setSource(index, eye.graph->getTexture(eye.outputs.color));
        }
        const auto& pose = tracked.view.pose;
        const auto  eyeWorld =
            glm::translate(glm::mat4(1), glm::vec3(pose.position.x, pose.position.y, pose.position.z)) *
            glm::mat4_cast(glm::quat(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z));
        const auto                 rig = glm::inverse(m_Rig.view());
        const vultra::RenderCamera camera {glm::inverse(rig * eyeWorld),
                                           tracked.viewProjection(0.05f, m_Rig.farPlane) * eyeWorld,
                                           0.05f,
                                           m_Rig.farPlane};
        eye.renderer->prepare(camera, *eye.graph, eye.outputs);
        eye.graph->execute(cmd);
        // Linear float source: the XR attachment performs sRGB encoding when required by its format.
        m_EyeBlit.draw(cmd, *tracked.color, {0, 0, size.width, size.height}, index);
    }

    struct EyeRenderer
    {
        std::unique_ptr<vultra::BuiltinRenderer> renderer;
        std::unique_ptr<vultra::RenderGraph>     graph;
        vultra::BuiltinRenderer::Outputs         outputs {};
        vultra::Extent                           size {};
    };

    vultra::Environment        m_Environment;
    vultra::GpuScene           m_Scene;
    vultra::TextureBlit        m_EyeBlit;
    std::array<EyeRenderer, 2> m_Eyes;
    vultra::FpsCamera          m_Rig;
    float                      m_DeltaSeconds = 0;
};

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("example-openxr-sponza", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Original Sponza assets through the per-eye OpenPBR renderer and desktop mirror");
    vultra::addAppOptions(cli);
    vultra::addAssetImportOptions(cli);
    sample::addCaptureOption(cli);
    if (!vultra::parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    XrSponzaApp app(cli);
    app.run(sample::getOptions(cli).frames);
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
