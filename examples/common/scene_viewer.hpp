#pragma once

#include "debug_lines.hpp"
#include "material_scene.hpp"
#include "sample.hpp"

#include <vultra/core/os/file_dialog.hpp>
#include <vultra/core/profiling/profiler.hpp>
#include <vultra/function/app/imgui_app.hpp>
#include <vultra/function/asset/asset_options.hpp>
#include <vultra/function/camera/fps_camera.hpp>
#include <vultra/function/camera/orbit_camera.hpp>
#include <vultra/function/renderer/builtin/builtin_renderer.hpp>

#include <glm/gtc/type_ptr.hpp>

class SceneViewer final : public vultra::ImGuiApp
{
public:
    SceneViewer(const argparse::ArgumentParser& cli,
                std::string                     title,
                bool                            walkthrough = false,
                bool                            debugDraw   = false) :
        ImGuiApp({.title    = title,
                  .size     = {1280, 800},
                  .features = cli.get<bool>("--meshlets") ? VriFeature_MeshShader : 0ull}),
        m_Title(std::move(title)),
        m_Walkthrough(walkthrough),
        m_DebugDraw(debugDraw),
        m_Meshlets(cli.get<bool>("--meshlets")),
        m_Options(sample::getOptions(cli)),
        m_ImportOptions(vultra::getAssetImportOptions(cli)),
        m_EnvironmentFile(cli.get<std::string>("--environment")),
        m_ShowUi(!cli.get<bool>("--no-ui")),
        m_Profiler(getDevice()),
        m_Environment(getDevice(), m_EnvironmentFile)
    {
        loadModel(cli.get<bool>("--materials") ? std::filesystem::path() :
                                                 std::filesystem::path(cli.get<std::string>("model")));
        if (const auto eye = cli.present<std::vector<float>>("--eye"))
        {
            m_Walkthrough        = true;
            m_FpsCamera.position = {eye->at(0), eye->at(1), eye->at(2)};
            glm::vec3 target     = m_Camera.center;
            if (const auto at = cli.present<std::vector<float>>("--look-at"))
            {
                target = {at->at(0), at->at(1), at->at(2)};
            }
            const auto direction = target - m_FpsCamera.position;
            const auto length    = glm::length(direction);
            if (!std::isfinite(length) || length < 1e-6f || glm::length(glm::vec2(direction.x, direction.z)) < 1e-6f)
            {
                throw std::invalid_argument("Camera eye/target must define a finite view not parallel to Y");
            }
            m_FpsCamera.yaw      = std::atan2(direction.x, -direction.z);
            m_FpsCamera.pitch    = std::asin(direction.y / length);
            m_FpsCamera.farPlane = length + m_Camera.radius * 4;
        }
        else if (cli.present<std::vector<float>>("--look-at"))
        {
            throw std::invalid_argument("--look-at requires --eye");
        }
        const auto shadows = cli.present<std::string>("--shadows").value_or("pcf");
        if (shadows == "off")
        {
            m_Renderer->settings.shadowFilter = vultra::ShadowFilter::eDisabled;
        }
        else if (shadows == "hard")
        {
            m_Renderer->settings.shadowFilter = vultra::ShadowFilter::eHard;
        }
        else if (shadows == "pcss")
        {
            m_Renderer->settings.shadowFilter = vultra::ShadowFilter::ePcss;
        }
        m_Renderer->settings.debugMode      = uint32_t(cli.present<int>("--debug").value_or(0));
        m_Renderer->settings.meshShading    = m_Meshlets && !cli.get<bool>("--indexed");
        m_Renderer->settings.meshletCulling = !cli.get<bool>("--no-meshlet-culling");
        m_Renderer->settings.meshletColors  = cli.get<bool>("--meshlet-colors");
        vultra::Logger::app().info("Geometry path: {}", m_Renderer->settings.meshShading ? "task + mesh" : "indexed");
    }

private:
    void onUpdate(float seconds) override
    {
        m_DeltaSeconds = seconds;
        if (m_PendingModel)
        {
            try
            {
                loadModel(*m_PendingModel);
                m_LoadError.clear();
            }
            catch (const std::exception& error)
            {
                m_LoadError = error.what();
                vultra::Logger::app().error("Model load failed: {}", m_LoadError);
            }
            m_PendingModel.reset();
        }
        m_Renderer->pollShaders();
        if (m_DebugLines)
        {
            m_DebugLines->pipeline->poll();
        }
    }

    void onImGui() override
    {
        if (m_ShowUi)
        {
            const auto origin = ImGui::GetMainViewport()->Pos;
            ImGui::SetNextWindowPos({origin.x + 16, origin.y + 16}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({370, 690}, ImGuiCond_FirstUseEver);
            ImGui::Begin(m_Title.c_str());
            if (m_Walkthrough)
            {
                ImGui::TextUnformatted("WASD / QE: move; RMB: look; Shift: faster");
                ImGui::SliderFloat("Camera speed", &m_FpsCamera.speed, 0.1f, 20);
            }
            else
            {
                ImGui::TextUnformatted("LMB: orbit; MMB / RMB: pan; wheel: zoom");
            }
            if (ImGui::Button("Open model..."))
            {
                try
                {
                    m_PendingModel = vultra::openModelDialog(getWindow());
                }
                catch (const std::exception& error)
                {
                    m_LoadError = error.what();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload"))
            {
                m_PendingModel = m_ModelPath;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reimport"))
            {
                m_ImportOptions.reimport = true;
                m_PendingModel           = m_ModelPath;
            }
            if (ImGui::Button("Material spheres"))
            {
                m_PendingModel = std::filesystem::path();
            }
            ImGui::TextWrapped("%s", m_ModelName.c_str());
            ImGui::Text("%zu draws", m_GpuScene->primitives.size());
            if (m_GpuScene->meshlets)
            {
                ImGui::Text("%u meshlets (64 vertices / 124 triangles max)", m_GpuScene->meshlets->count);
                ImGui::Checkbox("Mesh shading", &m_Renderer->settings.meshShading);
                ImGui::Checkbox("Meshlet frustum culling", &m_Renderer->settings.meshletCulling);
                ImGui::Checkbox("Meshlet colors", &m_Renderer->settings.meshletColors);
            }
            if (!m_LoadError.empty())
            {
                ImGui::TextWrapped("Load failed (current model kept): %s", m_LoadError.c_str());
            }
            ImGui::Separator();
            ImGui::PushItemWidth(160);
            auto& settings = m_Renderer->settings;
            ImGui::TextUnformatted("OpenPBR opaque subset / HDR IBL / CSM");
            ImGui::Checkbox("Debug bounds / grid / axes / sphere", &m_DebugDraw);
            ImGui::Checkbox("Skybox", &settings.skybox);
            ImGui::Checkbox("IBL", &settings.ibl);
            ImGui::SliderFloat("Environment", &settings.environmentIntensity, 0, 3);
            ImGui::SliderFloat("Exposure (EV)", &settings.exposure, -4, 4);
            ImGui::SliderFloat("Sun intensity", &settings.lightIntensity, 0, 10);
            ImGui::SliderFloat3("To sun", glm::value_ptr(settings.directionToLight), -1, 1);
            if (glm::length(settings.directionToLight) < 0.01f)
            {
                settings.directionToLight = {0, 1, 0};
            }
            int shadowMode = int(settings.shadowFilter);
            ImGui::Combo("Shadows", &shadowMode, "Off\0Hard\0PCF\0PCSS\0");
            settings.shadowFilter = vultra::ShadowFilter(shadowMode);
            ImGui::SliderFloat("Split lambda", &settings.splitLambda, 0, 1);
            ImGui::SliderFloat("Depth bias", &settings.shadowBias, 0, 0.003f, "%.5f");
            ImGui::SliderFloat("Normal bias", &settings.normalBias, 0, 3);
            ImGui::SliderFloat("Sun radius", &settings.sunAngularRadius, 0, 0.1f);
            ImGui::SliderFloat("Roughness override", &settings.roughnessOverride, -1, 1);
            ImGui::SliderFloat("Metalness override", &settings.metalnessOverride, -1, 1);
            int debugMode = int(settings.debugMode);
            ImGui::Combo("View", &debugMode, "Lit\0Base color\0Normals\0Cascades\0Shadow visibility\0Emission\0");
            settings.debugMode = uint32_t(debugMode);
            if (ImGui::Button("Rebuild IBL"))
            {
                m_Environment = vultra::Environment(getDevice(), m_EnvironmentFile);
            }
            for (const auto& timing : m_Profiler.timings())
            {
                ImGui::Text("%s: GPU %.3f ms", timing.name.c_str(), timing.gpuMs);
            }
            const auto diagnostics = m_Renderer->diagnostics();
            if (!diagnostics.empty() && ImGui::CollapsingHeader("Shader diagnostics"))
            {
                ImGui::TextWrapped("%s", diagnostics.c_str());
            }
            ImGui::PopItemWidth();
            ImGui::End();
        }
    }

    void onPreRender() override
    {
        ImGuiApp::onPreRender();
        if (m_Walkthrough)
        {
            m_FpsCamera.update(getWindow().input(), m_DeltaSeconds, getGui().inputCapture());
        }
        else
        {
            m_Camera.update(getWindow().input(), getWindow().size(), getGui().inputCapture());
        }
    }

    void onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
    {
        if (!m_Graph || m_GraphSize != getSwapchain().size())
        {
            m_GraphSize  = getSwapchain().size();
            m_Graph      = std::make_unique<vultra::RenderGraph>(getDevice());
            m_Outputs    = m_Renderer->addPasses(*m_Graph, m_GraphSize);
            m_Backbuffer = m_Graph->importResource("backbuffer", target, false);
            m_Graph->addPass(
                "Copy display",
                {{m_Outputs.color, vultra::Usage::eCopySource}, {m_Backbuffer, vultra::Usage::eCopyDestination}},
                [this](auto* cmd, auto& resources)
                {
                    VriTextureCopyDesc copy {};
                    copy.src.layerNum = 1;
                    copy.dst.layerNum = 1;
                    copy.src.aspect   = VriImageAspect_Color;
                    copy.dst.aspect   = VriImageAspect_Color;
                    getDevice().core.CmdCopyTexture(cmd,
                                                    resources.getTexture(m_Backbuffer).handle,
                                                    resources.getTexture(m_Outputs.color).handle,
                                                    &copy);
                });
            m_Graph->addPass(
                "Debug lines",
                {{m_Backbuffer, vultra::Usage::eColorReadWrite}, {m_Outputs.depth, vultra::Usage::eDepthRead}},
                [this](auto* cmd, auto& resources)
                {
                    if (m_DebugDraw && m_DebugLines)
                    {
                        m_DebugLines->draw(cmd,
                                           resources.getTexture(m_Backbuffer),
                                           nullptr,
                                           &resources.getTexture(m_Outputs.depth));
                    }
                });
            m_Graph->addPass(
                "ImGui",
                {{m_Backbuffer, vultra::Usage::eColorReadWrite}},
                [this](auto* cmd, auto& resources)
                {
                    getGui().copy(cmd);
                    vultra::beginColorPass(getDevice(), cmd, resources.getTexture(m_Backbuffer).view(), m_GraphSize);
                    getGui().draw(cmd);
                    getDevice().core.CmdEndRendering(cmd);
                });
            m_Graph->addPass(
                "Present",
                {{m_Backbuffer, vultra::Usage::ePresent}},
                [](auto*, auto&)
                {
                },
                true);
            m_Graph->compile();
            for (const auto& name : m_Graph->activePasses())
            {
                vultra::Logger::app().debug("Pass: {}", name);
            }
        }
        m_Graph->bind(m_Backbuffer, target);
        // Debug geometry extends beyond the model. Keep one projection for scene depth and lines.
        const float cameraDistance =
            glm::length((m_Walkthrough ? m_FpsCamera.position : m_Camera.position()) - m_GpuScene->center);
        const float                minimumFar = m_DebugDraw ? cameraDistance + m_DebugRadius * 1.01f : 0;
        const vultra::RenderCamera camera =
            m_Walkthrough ? m_FpsCamera.camera(m_GraphSize, minimumFar) : m_Camera.camera(m_GraphSize, minimumFar);
        if (m_DebugLines)
        {
            const auto matrix = camera.projection * camera.view;
            std::copy_n(glm::value_ptr(matrix), 16, m_DebugLines->parameters.transform.begin());
        }
        m_Renderer->prepare(camera, *m_Graph, m_Outputs);
        m_Graph->execute(cmd, &m_Profiler);
    }

    void onPostRender(vultra::Texture& target) override
    {
        m_Profiler.collect();
        sample::captureFrame(m_Options, frameCount(), getDevice(), target);
    }

    void loadModel(const std::filesystem::path& path)
    {
        vultra::ImportedAsset asset;
        if (path.empty())
        {
            asset.scene    = sample::makeMaterialScene();
            asset.textures = vultra::prepareTextures(asset.scene, m_ImportOptions.textures);
        }
        else
        {
            asset = vultra::importAsset(path, m_ImportOptions);
        }
        m_ImportOptions.reimport = false;
        const auto& scene        = asset.scene;
        auto gpuScene = std::make_unique<vultra::GpuScene>(getDevice(), asset, m_Meshlets, m_ImportOptions.workers);
        auto renderer =
            std::make_unique<vultra::BuiltinRenderer>(getDevice(), *gpuScene, m_Environment, getSwapchain().format());
        if (m_Renderer)
        {
            renderer->settings = m_Renderer->settings;
        }
        const auto lines      = sample::makeDebugLines(scene);
        auto       debugLines = std::make_unique<sample::ColoredMesh>(getDevice(),
                                                                getSwapchain().format(),
                                                                lines.vertices,
                                                                lines.indices,
                                                                VriPrimitiveTopology_LineList,
                                                                true);
        // Build all new GPU resources first. A failed load leaves the current model intact.
        m_Graph.reset();
        m_Renderer          = std::move(renderer);
        m_GpuScene          = std::move(gpuScene);
        m_DebugLines        = std::move(debugLines);
        m_DebugRadius       = lines.boundingRadius(scene.center);
        m_ModelPath         = path;
        const auto filename = path.filename().u8string();
        m_ModelName         = path.empty() ? "Material spheres" : std::string(filename.begin(), filename.end());
        getWindow().setTitle((m_Title + " | " + m_ModelName).c_str());
        m_Camera             = {};
        m_Camera.center      = scene.center;
        m_Camera.radius      = scene.radius;
        m_Camera.distance    = scene.radius * 2.5f;
        m_Camera.pitch       = 0.2f;
        m_FpsCamera.farPlane = scene.radius * 2;
        vultra::Logger::app().info("Loaded {}: {} triangles, {} draws",
                                   m_ModelName,
                                   scene.indices.size() / 3,
                                   scene.primitives.size());
    }

    std::string                              m_Title;
    bool                                     m_Walkthrough;
    bool                                     m_DebugDraw;
    float                                    m_DebugRadius = 0;
    bool                                     m_Meshlets;
    float                                    m_DeltaSeconds = 0;
    vultra::FpsCamera                        m_FpsCamera;
    sample::Options                          m_Options;
    vultra::AssetImportOptions               m_ImportOptions;
    std::filesystem::path                    m_EnvironmentFile;
    bool                                     m_ShowUi;
    vultra::Profiler                         m_Profiler;
    vultra::Environment                      m_Environment;
    std::unique_ptr<sample::ColoredMesh>     m_DebugLines;
    std::unique_ptr<vultra::GpuScene>        m_GpuScene;
    std::unique_ptr<vultra::BuiltinRenderer> m_Renderer;
    std::filesystem::path                    m_ModelPath;
    std::optional<std::filesystem::path>     m_PendingModel;
    std::string                              m_ModelName;
    std::string                              m_LoadError;
    vultra::OrbitCamera                      m_Camera;
    std::unique_ptr<vultra::RenderGraph>     m_Graph;
    vultra::BuiltinRenderer::Outputs         m_Outputs {};
    vultra::RenderGraph::Resource            m_Backbuffer {};
    vultra::Extent                           m_GraphSize {};
};

inline void addSceneViewerOptions(argparse::ArgumentParser& cli,
                                  std::string               defaultModel,
                                  std::string               defaultEnvironment = "",
                                  bool                      meshShading        = false)
{
    vultra::addAppOptions(cli);
    sample::addCaptureOption(cli);
    vultra::addAssetImportOptions(cli);
    cli.add_argument("model").default_value(std::move(defaultModel)).help("Static glTF/GLB, OBJ or FBX model");
    cli.add_argument("--materials").flag().help("Show procedural material spheres instead");
    cli.add_argument("--environment")
        .default_value(std::move(defaultEnvironment))
        .help("Radiance HDR environment image");
    cli.add_argument("--no-ui").flag();
    cli.add_argument("--meshlets")
        .default_value(meshShading)
        .implicit_value(true)
        .help("Build meshlets and require task/mesh shader support");
    cli.add_argument("--indexed").flag().help("Start with indexed drawing; keep meshlets available when requested");
    cli.add_argument("--meshlet-colors").flag().help("Color meshlets in the mesh shading path");
    cli.add_argument("--no-meshlet-culling").flag().help("Disable task-stage meshlet frustum culling");
    cli.add_argument("--eye").nargs(3).scan<'g', float>().help(
        "World-space camera position; enables first-person controls");
    cli.add_argument("--look-at")
        .nargs(3)
        .scan<'g', float>()
        .help("World-space camera target (default: scene center); requires --eye");
    cli.add_argument("--shadows").choices("off", "hard", "pcf", "pcss");
    cli.add_argument("--debug")
        .scan<'i', int>()
        .choices(0, 1, 2, 3, 4, 5)
        .help("0: lit, 1: albedo, 2: normals, 3: cascades, 4: shadow visibility, 5: emission");
}
