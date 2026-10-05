#include "../common/material_scene.hpp"
#include "../common/sample.hpp"

#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/imgui_app.hpp>
#include <vultra/scene/camera/orbit_camera.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/shader_runtime.hpp>
#include <vultra/servers/rendering/texture_blit.hpp>
#include <vultra/ui/editor_gui_shader_material.hpp>

namespace
{
    vultra::ShaderCompileOptions compileOptions()
    {
        vultra::ShaderCompileOptions options;
        options.includeDirectories = {"builtin/shaders", "external"};
        return options;
    }

    class ShaderExample final : public vultra::ImGuiApp
    {
    public:
        ShaderExample(bool edit, bool mesh, bool deferred, sample::Options options, std::filesystem::path layout) :
            ImGuiApp({.title    = "Vultra | Game Shader Materials",
                      .size     = {1280, 800},
                      .features = mesh ? VriFeature_MeshShader : 0ull},
                     {.iniFile = std::move(layout)}),
            m_Environment(getDevice()),
            m_Scene(getDevice(), sample::makeMaterialScene(), mesh),
            m_Shader(getDevice(),
                     edit ? "examples/shader/painted_metal.vshader" : "build/shaders/examples/painted_metal.vshaderc",
                     compileOptions(),
                     {},
                     {"Forward", "ShadowCaster", "GBufferBase", "GBufferMaterial"},
                     vultra::BuiltinRenderer::shaderSubshaderCompatibility),
            m_Renderer(getDevice(), m_Scene, m_Environment),
            m_Display(getDevice(), VriFormat_BGRA8_UNORM),
            m_Options(std::move(options))
        {
            m_Renderer.settings.path =
                deferred ? vultra::RenderPath::eNaiveDeferred : vultra::RenderPath::eNaiveForward;
            m_Renderer.settings.meshShading = mesh;
            m_Camera.center                 = {0, 1, 0};
            m_Camera.radius                 = 9;
            m_Camera.distance               = 19;
            // The ground retains its imported numeric material; every sphere uses one scoped shader instance.
            for (uint32_t slot = 1; slot < m_Scene.materials.size(); ++slot)
            {
                vultra::MaterialInstance instance;
                instance.set(m_Shader.asset(), "roughness", 0.07f + float((slot - 1) % 5) * 0.22f);
                instance.set(m_Shader.asset(), "metalness", float((slot - 1) / 5));
                m_Materials.push_back(m_Shader.addMaterial(std::move(instance)));
            }
            bindMaterials();
        }

    private:
        void bindMaterials()
        {
            for (uint32_t slot = 0; slot < m_Materials.size(); ++slot)
            {
                m_Renderer.setShaderMaterial(slot + 1, &m_Shader.material(m_Materials[slot]));
            }
        }

        void onUpdate(float) override
        {
        }

        void onImGui() override
        {
            auto       ui     = getEditorGui().frame();
            const auto origin = ui.mainViewportPos();
            ui.setNextWindowPos({origin.x + 16, origin.y + 16}, ImGuiCond_FirstUseEver);
            ui.setNextWindowSize({430, 620}, ImGuiCond_FirstUseEver);
            if (ui.beginWindow("Game shader material"))
            {
                ui.text("%s | generation %llu", m_Shader.asset().name.c_str(), m_Shader.generation());
                ui.combo("Sphere", &m_Selected, "1\0 2\0 3\0 4\0 5\0 6\0 7\0 8\0 9\0 10\0");
                vultra::drawShaderMaterialInspector(getEditorGui(),
                                                    m_Shader.asset(),
                                                    m_Shader.instance(m_Materials[m_Selected]));
                ui.textWrapped("Source editing: run with --edit. The default loads only the cooked game asset.");
                if (!m_Shader.diagnostics().empty() && ui.collapsingHeader("Shader diagnostics"))
                {
                    ui.textWrapped("%s", m_Shader.diagnostics().c_str());
                }
            }
            ui.endWindow();
        }

        void onPreRender() override
        {
            ImGuiApp::onPreRender();
            const auto size = getSwapchain().size();
            m_Camera.update(getWindow().input(), getWindow().size(), getEditorGui().inputCapture());
            if (!m_Graph || m_Size != size)
            {
                m_Graph   = std::make_unique<vultra::RenderGraph>(getDevice());
                m_Size    = size;
                m_Outputs = m_Renderer.addPasses(*m_Graph, size);
                m_Graph->exportResource(m_Outputs.color);
                m_Graph->compile();
                m_Display.setSource(0, m_Graph->getTexture(m_Outputs.color));
            }
            if (m_Shader.poll(
                    [&](uint32_t, vultra::ShaderMaterial& candidate)
                    {
                        m_Renderer.prepareShaderMaterial(candidate, *m_Graph, m_Outputs);
                    }))
            {
                bindMaterials();
            }
            m_Renderer.prepare(m_Camera.camera(size), *m_Graph, m_Outputs);
        }

        void onRender(VriCommandBuffer* commands, vultra::Texture& target) override
        {
            m_Graph->execute(commands);
            m_Display.draw(commands, target, {0, 0, m_Size.width, m_Size.height});
            drawGui(commands, target);
        }

        void onPostRender(vultra::Texture& target) override
        {
            sample::captureFrame(m_Options, frameCount(), getDevice(), target);
        }

        vultra::Environment                  m_Environment;
        vultra::GpuScene                     m_Scene;
        vultra::ShaderRuntime                m_Shader;
        vultra::BuiltinRenderer              m_Renderer;
        vultra::TextureBlit                  m_Display;
        std::unique_ptr<vultra::RenderGraph> m_Graph;
        vultra::BuiltinRenderer::Outputs     m_Outputs;
        vultra::Extent                       m_Size;
        vultra::OrbitCamera                  m_Camera;
        std::vector<uint32_t>                m_Materials;
        int                                  m_Selected = 0;
        sample::Options                      m_Options;
    };
} // namespace

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("example-shader", "0.1.0", argparse::default_arguments::none);
    vultra::addAppOptions(cli);
    sample::addCaptureOption(cli);
    cli.add_argument("--layout-file").help("Use an isolated ImGui layout file");
    cli.add_argument("--edit").flag().help("Load game source and enable transactional hot reload");
    cli.add_argument("--meshlets").flag().help("Use the shared task/mesh path");
    cli.add_argument("--deferred").flag().help("Render through the standard G-buffer passes");
    if (!vultra::parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    const auto    options = sample::getOptions(cli);
    ShaderExample app(cli.get<bool>("--edit"),
                      cli.get<bool>("--meshlets"),
                      cli.get<bool>("--deferred"),
                      options,
                      cli.present<std::string>("--layout-file").value_or(""));
    app.run(options.frames);
    vultra::Logger::app().info("Game shader materials: {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
