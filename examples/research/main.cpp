#include "color_gain.hpp"

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/core/base/property.hpp>
#include <vultra/drivers/profiling/benchmark.hpp>
#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/main/app/imgui_app.hpp>
#include <vultra/scene/camera/orbit_camera.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/scene/scene_render_state.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/ui/editor_gui_inspector.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vultra;

namespace
{
    std::string fileHash(const std::filesystem::path& path)
    {
        std::ifstream source(path, std::ios::binary);
        if (!source)
        {
            throw std::runtime_error("Cannot hash source file: " + path.string());
        }
        uint64_t hash = 14695981039346656037ull;
        for (char value; source.get(value);)
        {
            hash = (hash ^ uint8_t(value)) * 1099511628211ull;
        }
        return std::format("{:016x}", hash);
    }

    enum class PreviewKind
    {
        eColor,
        eHdr,
        ePosition,
        eNormal,
        eDepth
    };

    bool drawRenderPath(EditorGuiProperty, void* value, void* userData)
    {
        auto& gui = *static_cast<EditorGui*>(userData);
        return gui.frame().comboValue(static_cast<int*>(value), "Naive Deferred\0Naive Forward\0\0");
    }

    void preparePreview(Image& image, PreviewKind kind, const glm::vec3& center, float radius)
    {
        radius             = std::max(radius, 0.000001f);
        float nearestDepth = 1;
        if (kind == PreviewKind::eDepth)
        {
            for (size_t i = 0; i < image.rgba.size(); i += 4)
            {
                const float value = image.rgba[i];
                if (std::isfinite(value) && value < 1)
                {
                    nearestDepth = std::min(nearestDepth, value);
                }
            }
        }
        const float depthRange = std::max(1 - nearestDepth, 0.000001f);
        for (size_t i = 0; i < image.rgba.size(); i += 4)
        {
            const float alpha = image.rgba[i + 3];
            for (size_t channel = 0; channel < 3; ++channel)
            {
                float& value = image.rgba[i + channel];
                if (!std::isfinite(value))
                {
                    value = 0;
                }
                else if (kind == PreviewKind::ePosition)
                {
                    value = alpha < 0 ? 0 : 0.5f + (value - center[channel]) / (2 * radius);
                }
                else if (kind == PreviewKind::eNormal)
                {
                    value = 0.5f * value + 0.5f;
                }
                else if (kind == PreviewKind::eHdr)
                {
                    value = std::max(value, 0.0f) / (1 + std::max(value, 0.0f));
                }
                else if (kind == PreviewKind::eDepth)
                {
                    value = (value - nearestDepth) / depthRange;
                }
            }
            image.rgba[i + 3] = 1;
        }
    }

    std::string projectHash(const ProjectManifest& project, const std::filesystem::path& projectFile)
    {
        const auto                                                 root = projectFile.parent_path();
        std::vector<std::pair<std::string, std::filesystem::path>> files {
            {projectFile.filename().generic_string(), projectFile},
            {project.mainScene.generic_string(), root / project.mainScene}};
        for (const auto& asset : project.assets())
        {
            files.emplace_back(asset.path.generic_string(), root / asset.path);
        }
        for (const auto& script : project.scripts)
        {
            files.emplace_back(script.path.generic_string(), root / script.path);
        }
        std::sort(files.begin(), files.end());
        uint64_t hash = 14695981039346656037ull;
        for (const auto& [name, path] : files)
        {
            for (const char value : name + '\0' + fileHash(path))
            {
                hash = (hash ^ uint8_t(value)) * 1099511628211ull;
            }
        }
        return std::format("{:016x}", hash);
    }

    std::string shaderHash()
    {
        std::vector<std::filesystem::path> files;
        for (const auto* root : {"builtin/shaders", "external/openpbr", "examples/research/shaders"})
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
            {
                const auto extension = entry.path().extension();
                if (entry.is_regular_file() && (extension == ".slang" || extension == ".slangh" || extension == ".h"))
                {
                    files.push_back(entry.path());
                }
            }
        }
        std::sort(files.begin(), files.end());
        uint64_t hash = 14695981039346656037ull;
        for (const auto& path : files)
        {
            const auto name = path.generic_string();
            for (const char value : name)
            {
                hash = (hash ^ uint8_t(value)) * 1099511628211ull;
            }
            hash = (hash ^ 0u) * 1099511628211ull;
            std::ifstream source(path, std::ios::binary);
            if (!source)
            {
                throw std::runtime_error("Cannot hash shader source: " + path.string());
            }
            for (char value; source.get(value);)
            {
                hash = (hash ^ uint8_t(value)) * 1099511628211ull;
            }
        }
        return std::format("{:016x}", hash);
    }

} // namespace

class ResearchApp final : public ImGuiApp
{
public:
    explicit ResearchApp(const argparse::ArgumentParser& cli) :
        ImGuiApp({.title          = "Vultra | Research - Builtin Renderer",
                  .size           = {1024, 768},
                  .renderDocFrame = cli.present<uint64_t>("--renderdoc-frame")},
                 {.persistLayout = !cli.present<std::string>("--benchmark").has_value(),
                  .iniFile       = cli.present<std::string>("--layout-file").value_or("")}),
        m_DumpDirectory(cli.present<std::string>("--dump").value_or("")),
        m_CapturePath(cli.present<std::string>("--capture").value_or("")),
        m_ReferencePath(cli.present<std::string>("--compare").value_or("")),
        m_BenchmarkDirectory(cli.present<std::string>("--benchmark").value_or("")),
        m_IntermediateDirectory(cli.present<std::string>("--dump-intermediates").value_or("")),
        m_IntermediateRequested(!m_IntermediateDirectory.empty()),
        m_PreviewEnabled(cli.get<bool>("--preview-intermediates")),
        m_SourceRevision(cli.present<std::string>("--revision").value_or("")),
        m_WarmupFrames(cli.present<uint64_t>("--warmup").value_or(60)),
        m_SampleFrames(cli.present<uint64_t>("--samples").value_or(120)),
        m_ModelPath(cli.get<std::string>("--model")),
        m_EnvironmentPath(cli.get<std::string>("--environment")),
        m_Profiler(getDevice()),
        m_FixedCamera(cli.get<bool>("--fixed-camera"))
    {
        getPassCatalog().add(research::colorGainDefinition());
        if (const auto definition = cli.present<std::string>("--graph"))
        {
            m_GraphDefinitionPath = *definition;
            m_GraphDefinition     = GraphDefinition::load(m_GraphDefinitionPath);
        }
        else if (const auto gain = cli.present<double>("--color-gain"))
        {
            m_GraphDefinition = GraphDefinition {{{"gain", "research.color_gain", {{"gain", *gain}}}},
                                                 {{"scene.hdr", "gain.source"}},
                                                 {"gain.color"}};
        }
        getEditorGui().setPropertyDrawer("path", {drawRenderPath, &getEditorGui()});
        if (const auto projectFile = cli.present<std::string>("--project"))
        {
            m_ProjectPath = *projectFile;
            m_Project     = ProjectManifest::load(m_ProjectPath);
            m_SceneTree   = SceneTree::load(m_ProjectPath.parent_path() / m_Project->mainScene);
            m_SceneTree->validateAssets(*m_Project);
            if (!cli.is_used("--environment"))
            {
                m_EnvironmentPath = sceneEnvironmentPath(*m_SceneTree, *m_Project, m_ProjectPath.parent_path());
            }
        }
        m_Environment = std::make_unique<Environment>(getDevice(), m_EnvironmentPath);
        auto asset    = m_Project ?
                            importScene(*m_SceneTree, *m_Project, m_ProjectPath.parent_path(), {}, &m_SceneInstances) :
                            importAsset(m_ModelPath);
        m_AssetCachePath = asset.cachePath;
        m_GpuScene       = getRenderingServer().uploadScene(asset);
        m_Renderer =
            std::make_unique<BuiltinRenderer>(getDevice(), *m_GpuScene, *m_Environment, getSwapchain().format());
        m_Renderer->settings.path =
            cli.get<std::string>("--path") == "forward" ? RenderPath::eNaiveForward : RenderPath::eNaiveDeferred;
        m_Camera.center   = m_GpuScene->center;
        m_Camera.radius   = m_GpuScene->radius;
        m_Camera.distance = m_GpuScene->radius * 2.5f;
        m_Camera.pitch    = 0.2f;
        if (!m_DumpDirectory.empty())
        {
            std::filesystem::create_directories(m_DumpDirectory);
            if (std::filesystem::exists(m_DumpDirectory / "timings.csv"))
            {
                throw std::runtime_error("Use a fresh dump directory");
            }
            m_Timings.open(m_DumpDirectory / "timings.csv");
            if (!m_Timings)
            {
                throw std::runtime_error("Cannot create timings.csv");
            }
            m_Timings << "frame,pass,cpu_record_ms,gpu_ms\n";
        }
    }

    ~ResearchApp() override
    {
        forgetGraphTextures();
        getEditorGui().removePropertyDrawer("path");
    }

    void saveResults()
    {
        if (m_Graph && (!m_CapturePath.empty() || !m_ReferencePath.empty()))
        {
            const auto image = readback(getDevice(), m_Graph->getTexture(m_Scene));
            if (!m_CapturePath.empty())
            {
                savePng(image, m_CapturePath);
            }
            if (!m_ReferencePath.empty())
            {
                const auto metrics = compare(loadPng(m_ReferencePath), image);
                Logger::app().info("PSNR {} dB, SSIM {}, MSE {}", metrics.psnr, metrics.ssim, metrics.mse);
            }
        }
        if (!m_BenchmarkDirectory.empty())
        {
            if (m_Benchmark.size() != m_SampleFrames)
            {
                throw std::runtime_error("Benchmark ended before all measured frames completed");
            }
            BenchmarkMetadata metadata;
            metadata.experiment     = "example-research";
            metadata.sourceRevision = m_SourceRevision;
            metadata.shaderHash     = shaderHash();
            metadata.width          = m_GraphSize.width;
            metadata.height         = m_GraphSize.height;
            metadata.warmupFrames   = m_WarmupFrames;
#ifdef NDEBUG
            metadata.buildMode = "release";
#else
            metadata.buildMode = "debug";
#endif
            metadata.validation = true;
#if defined(VULTRA_WINDOW_SDL3)
            metadata.windowSystem = "sdl3";
#else
            metadata.windowSystem = "glfw";
#endif
            if (const char* requested = std::getenv("VULTRA_WINDOW_SYSTEM"))
            {
                metadata.windowSystem += std::string("/") + requested;
            }
            const auto eye =
                m_SceneState.camera ? glm::vec3(glm::inverse(m_SceneState.camera->view)[3]) : m_Camera.position();
            metadata.parameters = {
                {"path", m_Outputs.path == RenderPath::eNaiveDeferred ? "NaiveDeferred" : "NaiveForward"},
                {"environment", m_EnvironmentPath.generic_string()},
                {"environment_hash_fnv1a64", fileHash(m_EnvironmentPath)},
                {"camera_eye", std::format("{:.6g},{:.6g},{:.6g}", eye.x, eye.y, eye.z)},
                {"exposure", std::format("{:.6g}", m_Renderer->settings.exposure)},
                {"shadow_filter", std::to_string(int(m_Renderer->settings.shadowFilter))},
                {"gui", "enabled"}};
            if (m_GraphDefinition)
            {
                metadata.parameters.emplace_back("graph_definition", m_GraphDefinition->serialize());
                metadata.parameters.emplace_back("graph_construction",
                                                 m_GraphDefinitionPath.empty() ? "cpp" : "definition");
            }
            metadata.parameters.emplace_back("capture", m_CapturePath.generic_string());
            if (m_Project)
            {
                metadata.parameters.emplace_back("project", m_ProjectPath.generic_string());
                metadata.parameters.emplace_back("project_hash_fnv1a64", projectHash(*m_Project, m_ProjectPath));
            }
            else
            {
                metadata.parameters.emplace_back("model", m_ModelPath.generic_string());
                metadata.parameters.emplace_back("asset_cache_hash_fnv1a64", fileHash(m_AssetCachePath));
            }
            m_Benchmark.write(m_BenchmarkDirectory, metadata, *getDevice().core.GetDeviceDesc(getDevice().handle));
        }
        if (m_Timings.is_open())
        {
            m_Timings.flush();
            if (!m_Timings)
            {
                throw std::runtime_error("Writing timings.csv failed");
            }
        }
    }

private:
    void forgetGraphTextures()
    {
        if (!m_Graph || !m_GraphSnapshot)
        {
            return;
        }
        getEditorGui().forgetTexture(m_Graph->getTexture(m_Scene));
        for (size_t i = 0; i < m_ViewedAttachments.size(); ++i)
        {
            if (m_ViewedAttachments[i])
            {
                const auto resource = i == 0 ? m_Outputs.hdr : m_Outputs.gbuffer[i - 1];
                getEditorGui().forgetTexture(m_Graph->getTexture(resource));
            }
        }
        m_ViewedAttachments.fill(false);
    }

    void onUpdate(float) override
    {
        if (m_BenchmarkDirectory.empty())
        {
            m_Renderer->pollShaders();
        }
        m_Screenshot = false;
    }

    void onImGui() override
    {
        auto ui = getEditorGui().frame();
        ui.setNextWindowSize({400, 350}, ImGuiCond_FirstUseEver);
        {
            EditorGuiWindow experimentWindow(ui, "Experiment");
            if (experimentWindow)
            {
                ui.textWrapped("%s", (m_Project ? m_ProjectPath : m_ModelPath).filename().string().c_str());
                ui.text("%zu draws", m_GpuScene->primitives.size());
                if (m_SceneTree)
                {
                    ui.text("Scene: %s", m_SceneTree->root().name().c_str());
                    if (m_SceneTree->currentCamera().value != 0)
                    {
                        ui.textDisabled("Using the scene camera");
                    }
                    if (m_SceneTree->usesSceneLighting())
                    {
                        ui.textWrapped("Scene lights are active; the sun preset below is unused.");
                    }
                }
                ui.beginDisabled(!m_BenchmarkDirectory.empty());
                auto& settings = m_Renderer->settings;
                if (EditorGuiInspector inspector(getEditorGui(), "Settings"); inspector)
                {
                    inspector.properties(getObjectTypeCatalog().type("RenderSettings"), &settings);
                }
                m_Screenshot = ui.button("Save scene PNG");
                ui.sameLine();
                if (ui.button("Dump intermediate textures"))
                {
                    m_IntermediateDirectory =
                        std::filesystem::path("captures") / "intermediates" /
                        std::format("{}-{}", std::chrono::system_clock::now().time_since_epoch().count(), frameCount());
                    m_IntermediateRequested = true;
                }
                ui.endDisabled();
                const auto diagnostics = m_Renderer->diagnostics();
                if (!diagnostics.empty())
                {
                    ui.textWrapped("%s", diagnostics.c_str());
                }
                if (!m_Status.empty())
                {
                    ui.textWrapped("%s", m_Status.c_str());
                }
                ui.separatorText("Previous frame - CPU recording / GPU execution");
                for (const auto& timing : m_Profiler.timings())
                {
                    ui.text("%s: CPU %.3f ms (barrier %.3f), GPU %.3f ms (barrier %.3f)",
                            timing.name.c_str(),
                            timing.cpuMs,
                            timing.cpuBarrierMs,
                            timing.gpuMs,
                            timing.gpuBarrierMs);
                }
                if (!m_Profiler.hasGpuTimings())
                {
                    ui.textUnformatted("GPU timestamps unavailable");
                }
            }
        }

        if (!m_GraphSnapshot || m_GraphSize != getSwapchain().size() || m_GraphPath != m_Renderer->settings.path ||
            m_GraphCapture != m_IntermediateRequested)
        {
            return;
        }
        ui.setNextWindowPos({480, 60}, ImGuiCond_FirstUseEver);
        ui.setNextWindowSize({470, 560}, ImGuiCond_FirstUseEver);
        EditorGuiWindow graphWindow(ui, "RenderGraph");
        if (!graphWindow)
        {
            return;
        }
        ui.text("%zu passes, %zu resources", m_GraphSnapshot->passes.size(), m_GraphSnapshot->resources.size());
        ui.beginDisabled(!m_BenchmarkDirectory.empty());
        if (EditorGuiInspector inspector(getEditorGui(), "Preview", 0.62f); inspector)
        {
            inspector.boolField({"preview_color", "Preview color attachments"}, &m_PreviewEnabled);
        }
        ui.endDisabled();
        if (m_GraphPreview != m_PreviewEnabled)
        {
            return;
        }
        if (m_PreviewEnabled && m_GraphPreview && m_BenchmarkDirectory.empty())
        {
            constexpr const char* names[] {"Scene HDR",
                                           "Position / metallic",
                                           "Normal / roughness",
                                           "Albedo / weight",
                                           "Emission / occlusion",
                                           "Specular",
                                           "Geometric normal / IOR",
                                           "Coat"};
            const int             count = m_Outputs.path == RenderPath::eNaiveDeferred ? 8 : 1;
            m_PreviewIndex              = std::min(m_PreviewIndex, count - 1);
            if (EditorGuiInspector inspector(getEditorGui(), "Attachment"); inspector)
            {
                inspector.choice({"attachment", "Attachment"}, &m_PreviewIndex, names, count);
            }
            const auto  resource = m_PreviewIndex == 0 ? m_Outputs.hdr : m_Outputs.gbuffer[m_PreviewIndex - 1];
            auto&       texture  = m_Graph->getTexture(resource);
            const float width    = std::min(ui.contentRegionAvail().x, 420.0f);
            ui.image(getEditorGui().textureId(texture), {width, width * texture.desc.height / texture.desc.width});
            m_ViewedAttachments[m_PreviewIndex] = true;
            ui.textDisabled("Raw RGB; signed values clip. Dump for mapped PNGs and depth.");
        }
        for (size_t index = 0; index < m_GraphSnapshot->passes.size(); ++index)
        {
            const auto& pass = m_GraphSnapshot->passes[index];
            ui.pushId(int(index));
            if (ui.treeNodeEx("pass",
                              ImGuiTreeNodeFlags_DefaultOpen,
                              "%s  %s",
                              pass.active ? "[active]" : "[culled]",
                              pass.name.c_str()))
            {
                for (const auto dependency : pass.dependencies)
                {
                    ui.bulletText("after %s", m_GraphSnapshot->passes[dependency].name.c_str());
                }
                for (const auto& use : pass.uses)
                {
                    ui.bulletText("%s: %s",
                                  m_GraphSnapshot->resources[use.resourceIndex].name.c_str(),
                                  usageName(use.usage));
                }
                ui.treePop();
            }
            ui.popId();
        }
        ui.separatorText("Resources");
        for (size_t index = 0; index < m_GraphSnapshot->resources.size(); ++index)
        {
            const auto& resource = m_GraphSnapshot->resources[index];
            ui.text("%s  %s%s%s",
                    resource.name.c_str(),
                    resource.active ? "active" : "culled",
                    resource.imported ? ", imported" : "",
                    resource.exported ? ", exported" : "");
            if (resource.isTexture)
            {
                ui.sameLine();
                ui.textDisabled("%ux%u", resource.textureDesc.width, resource.textureDesc.height);
            }
            if (index == m_Scene.index && m_Graph && m_BenchmarkDirectory.empty())
            {
                ui.image(getEditorGui().textureId(m_Graph->getTexture(m_Scene)), {320, 240});
            }
        }
    }

    void onPreRender() override
    {
        ImGuiApp::onPreRender();
        if (m_BenchmarkDirectory.empty() && !m_FixedCamera && (!m_SceneTree || m_SceneTree->currentCamera().value == 0))
        {
            m_Camera.update(getWindow().input(), getWindow().size(), getEditorGui().inputCapture());
        }
    }

    void onRender(VriCommandBuffer* cmd, Texture& target) override
    {
        // Graph-owned textures persist until the size, render path or capture plan changes.
        if (!m_Graph || m_GraphSize != getSwapchain().size() || m_GraphPath != m_Renderer->settings.path ||
            m_GraphCapture != m_IntermediateRequested || m_GraphPreview != m_PreviewEnabled)
        {
            forgetGraphTextures();
            m_GraphSnapshot.reset();
            m_Graph.reset();
            m_GraphDefinitionState = {};
            m_Graph                = std::make_unique<RenderGraph>(getDevice());
            m_GraphSize            = getSwapchain().size();
            m_GraphPath            = m_Renderer->settings.path;
            m_Outputs              = m_Renderer->addScenePasses(*m_Graph, m_GraphSize);
            m_SkyboxCapture.reset();
            if (m_IntermediateRequested)
            {
                m_SkyboxCapture = m_Graph->captureAfterPass("Skybox", m_Outputs.hdr, "skybox_only");
            }
            if (m_GraphDefinition)
            {
                const std::array imports {GraphBinding {"scene.hdr", m_Outputs.hdr}};
                if (m_GraphDefinitionPath.empty())
                {
                    const std::array inputs {m_Outputs.hdr};
                    auto             pass = getPassCatalog().build(*m_Graph,
                                                                   "research.color_gain",
                                                                   "gain",
                                                                   inputs,
                                                                   m_GraphDefinition->passes.front().parameters);
                    m_GraphDefinitionState.outputs.push_back({"gain.color", pass.outputs.front()});
                    m_GraphDefinitionState.passes.push_back(std::move(pass));
                }
                else
                {
                    m_GraphDefinitionState = m_GraphDefinition->build(*m_Graph, getPassCatalog(), imports);
                }
                if (m_GraphDefinitionState.outputs.size() != 1)
                {
                    throw std::invalid_argument("Research requires one linear HDR graph output");
                }
                const auto output = m_GraphDefinitionState.outputs.front().resource;
                const auto info   = m_Graph->resourceInfo(output);
                if (!info.isTexture || info.textureDesc.format != VriFormat_RGBA16_SFLOAT ||
                    info.textureDesc.width != m_GraphSize.width || info.textureDesc.height != m_GraphSize.height)
                {
                    throw std::invalid_argument("Research graph output must match the RGBA16_SFLOAT scene extent");
                }
                m_Outputs.hdr = output;
            }
            m_Outputs.color = m_Renderer->addToneMappingPass(*m_Graph, m_Outputs.hdr, m_GraphSize);
            m_GraphCapture  = m_IntermediateRequested;
            m_GraphPreview  = m_PreviewEnabled;
            m_Scene         = m_Outputs.color;
            m_Backbuffer    = m_Graph->importResource("backbuffer", target, false);

            m_Graph->addPass("Copy scene",
                             {{m_Scene, Usage::eCopySource}, {m_Backbuffer, Usage::eCopyDestination}},
                             [this](auto* cmd, auto& g)
                             {
                                 VriTextureCopyDesc copy {};
                                 copy.src.layerNum = 1;
                                 copy.dst.layerNum = 1;
                                 copy.src.aspect   = VriImageAspect_Color;
                                 copy.dst.aspect   = VriImageAspect_Color;
                                 getDevice().core.CmdCopyTexture(cmd,
                                                                 g.getTexture(m_Backbuffer).handle,
                                                                 g.getTexture(m_Scene).handle,
                                                                 &copy);
                             });

            const auto drawGui = [this](auto* cmd, auto& g)
            {
                getEditorGui().copy(cmd);
                beginColorPass(getDevice(), cmd, g.getTexture(m_Backbuffer).view(), m_GraphSize);
                getEditorGui().draw(cmd);
                getDevice().core.CmdEndRendering(cmd);
            };
            if (m_GraphPreview && m_Outputs.path == RenderPath::eNaiveDeferred)
            {
                const auto& gbuffer = m_Outputs.gbuffer;
                m_Graph->addPass("ImGui",
                                 {{m_Scene, Usage::eSampled},
                                  {m_Backbuffer, Usage::eColorReadWrite},
                                  {m_Outputs.hdr, Usage::eSampled},
                                  {gbuffer[0], Usage::eSampled},
                                  {gbuffer[1], Usage::eSampled},
                                  {gbuffer[2], Usage::eSampled},
                                  {gbuffer[3], Usage::eSampled},
                                  {gbuffer[4], Usage::eSampled},
                                  {gbuffer[5], Usage::eSampled},
                                  {gbuffer[6], Usage::eSampled}},
                                 drawGui);
            }
            else if (m_GraphPreview)
            {
                m_Graph->addPass("ImGui",
                                 {{m_Scene, Usage::eSampled},
                                  {m_Backbuffer, Usage::eColorReadWrite},
                                  {m_Outputs.hdr, Usage::eSampled}},
                                 drawGui);
            }
            else
            {
                m_Graph->addPass("ImGui",
                                 {{m_Scene, Usage::eSampled}, {m_Backbuffer, Usage::eColorReadWrite}},
                                 drawGui);
            }

            m_Graph->addPass(
                "Present",
                {{m_Backbuffer, Usage::ePresent}},
                [](auto*, auto&)
                {
                },
                true);

            m_Graph->exportResource(m_Scene); // make it observable for research readback
            m_Graph->compile();
            m_GraphSnapshot = m_Graph->snapshot();
        }

        m_Graph->bind(m_Backbuffer, target);
        if (m_SceneTree)
        {
            m_GpuSync.update(*m_SceneTree, m_SceneInstances, *m_GpuScene);
            m_SceneState.update(*m_SceneTree, m_GraphSize);
        }
        m_Renderer->prepare(m_SceneState.camera.value_or(m_Camera.camera(m_GraphSize)),
                            *m_Graph,
                            m_Outputs,
                            m_SceneState.lighting(),
                            m_SceneState.environmentIntensity);
        m_Graph->execute(cmd, &m_Profiler);
    }

    void dumpIntermediates()
    {
        if (std::filesystem::exists(m_IntermediateDirectory))
        {
            throw std::runtime_error("Intermediate dump directory already exists: " + m_IntermediateDirectory.string());
        }
        std::filesystem::create_directories(m_IntermediateDirectory);
        const auto save = [this](RenderGraph::Resource resource, PreviewKind kind)
        {
            const auto& name  = m_GraphSnapshot->resources[resource.index].name;
            auto        image = readback(getDevice(), m_Graph->getTexture(resource));
            preparePreview(image, kind, m_GpuScene->center, m_GpuScene->radius);
            savePng(image, m_IntermediateDirectory / (name + ".png"));
        };
        for (const auto& shadow : m_Outputs.shadows)
        {
            save(shadow, PreviewKind::eDepth);
        }
        save(m_Outputs.depth, PreviewKind::eDepth);
        if (m_Outputs.path == RenderPath::eNaiveDeferred)
        {
            constexpr std::array kinds {PreviewKind::ePosition,
                                        PreviewKind::eNormal,
                                        PreviewKind::eColor,
                                        PreviewKind::eHdr,
                                        PreviewKind::eColor,
                                        PreviewKind::eNormal,
                                        PreviewKind::eColor};
            for (size_t i = 0; i < m_Outputs.gbuffer.size(); ++i)
            {
                save(m_Outputs.gbuffer[i], kinds[i]);
            }
        }
        if (m_SkyboxCapture)
        {
            save(*m_SkyboxCapture, PreviewKind::eHdr);
        }
        save(m_Outputs.hdr, PreviewKind::eHdr);
        save(m_Outputs.color, PreviewKind::eColor);
        m_Status = "Saved intermediate textures in " + m_IntermediateDirectory.string();
        Logger::app().info("{}", m_Status);
    }

    void onPostRender(Texture&) override
    {
        m_Profiler.collect();
        if (m_IntermediateRequested)
        {
            dumpIntermediates();
            m_IntermediateRequested = false;
        }

        if (m_Timings.is_open())
        {
            for (const auto& t : m_Profiler.timings())
            {
                m_Timings << frameCount() << ',' << t.name << ',' << t.cpuMs << ',' << t.gpuMs << '\n';
            }
        }

        if (m_Screenshot || !m_DumpDirectory.empty())
        {
            const auto image = readback(getDevice(), m_Graph->getTexture(m_Scene));
            if (!m_DumpDirectory.empty())
            {
                dumpFrame(image, m_DumpDirectory, frameCount());
            }
            if (m_Screenshot)
            {
                const auto directory = std::filesystem::path("captures") /
                                       std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                dumpFrame(image, directory, frameCount());
                m_Status = "Saved native-resolution scene in " + directory.string();
            }
        }
    }

    void onFrameComplete(const FrameTiming& frame) override
    {
        if (m_BenchmarkDirectory.empty() || frame.frameIndex < m_WarmupFrames)
        {
            return;
        }
        if (m_Benchmark.size() == 0)
        {
            m_MeasuredSize = getSwapchain().size();
        }
        if (getSwapchain().size() != m_MeasuredSize)
        {
            throw std::runtime_error("Benchmark framebuffer size changed during measurement");
        }
        m_Benchmark.add(frame, m_Profiler.timings());
    }

    std::filesystem::path                m_DumpDirectory;
    std::filesystem::path                m_CapturePath;
    std::filesystem::path                m_ReferencePath;
    std::filesystem::path                m_BenchmarkDirectory;
    std::filesystem::path                m_IntermediateDirectory;
    bool                                 m_IntermediateRequested = false;
    bool                                 m_PreviewEnabled        = false;
    std::string                          m_SourceRevision;
    uint64_t                             m_WarmupFrames;
    uint64_t                             m_SampleFrames;
    BenchmarkCapture                     m_Benchmark;
    Extent                               m_MeasuredSize {};
    std::filesystem::path                m_ModelPath;
    std::filesystem::path                m_ProjectPath;
    std::filesystem::path                m_EnvironmentPath;
    std::filesystem::path                m_AssetCachePath;
    std::optional<ProjectManifest>       m_Project;
    std::optional<SceneTree>             m_SceneTree;
    SceneRenderState                     m_SceneState;
    SceneGpuSync                         m_GpuSync;
    std::vector<SceneMeshInstance>       m_SceneInstances;
    Profiler                             m_Profiler;
    std::unique_ptr<Environment>         m_Environment;
    GpuSceneHandle                       m_GpuScene;
    std::unique_ptr<BuiltinRenderer>     m_Renderer;
    OrbitCamera                          m_Camera;
    bool                                 m_FixedCamera;
    std::filesystem::path                m_GraphDefinitionPath;
    std::optional<GraphDefinition>       m_GraphDefinition;
    GraphBuild                           m_GraphDefinitionState;
    std::unique_ptr<RenderGraph>         m_Graph;
    std::optional<RenderGraph::Snapshot> m_GraphSnapshot;
    std::optional<RenderGraph::Resource> m_SkyboxCapture;
    bool                                 m_GraphCapture = false;
    bool                                 m_GraphPreview = false;
    std::array<bool, 8>                  m_ViewedAttachments {};
    int                                  m_PreviewIndex = 3;
    BuiltinRenderer::Outputs             m_Outputs {};
    RenderGraph::Resource                m_Scene {};
    RenderPath                           m_GraphPath = RenderPath::eNaiveDeferred;
    RenderGraph::Resource                m_Backbuffer {};
    Extent                               m_GraphSize {};
    std::string                          m_Status;
    std::ofstream                        m_Timings;
    bool                                 m_Screenshot = false;
};

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("example-research", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Builtin OpenPBR renderer, RenderGraph observer, benchmarks and captures");
    vultra::addAppOptions(cli);
    cli.add_argument("--project").help("Load static mesh instances from a Vultra project scene");
    cli.add_argument("--model")
        .default_value(std::string("resources/models/DamagedHelmet/DamagedHelmet.glb"))
        .help("Static scene for renderer research");
    cli.add_argument("--environment")
        .default_value(std::string("resources/textures/environment_maps/citrus_orchard_puresky_1k.hdr"))
        .help("HDR environment image");
    cli.add_argument("--path")
        .default_value(std::string("deferred"))
        .choices("deferred", "forward")
        .help("BuiltinRenderer path (default: deferred)");
    cli.add_argument("--graph").help("Append a version-1 .vgraph project pipeline before tone mapping");
    cli.add_argument("--color-gain").scan<'g', double>().help("Build the HDR color gain pass directly in C++ (0..8)");
    cli.add_argument("--fixed-camera").flag().help("Keep the initial camera for repeatable captures");
    cli.add_argument("--dump").help("Dump scene PNGs and timings.csv into a new directory");
    cli.add_argument("--dump-intermediates").help("Save one frame of G-buffer, depth, shadow and HDR PNGs");
    cli.add_argument("--preview-intermediates").flag().help("Open the color attachment preview at startup");
    cli.add_argument("--capture").help("Save the final scene image");
    cli.add_argument("--compare").help("Compare the final scene against this PNG");
    cli.add_argument("--benchmark").help("Write an uncaptured benchmark report to a new directory");
    cli.add_argument("--warmup").scan<'u', uint64_t>().help("Warmup frames before benchmark (default 60)");
    cli.add_argument("--samples").scan<'u', uint64_t>().help("Measured frames (default 120)");
    cli.add_argument("--revision").help("Required source revision for benchmark metadata");
    cli.add_argument("--layout-file").help("Use an isolated ImGui layout file");
    cli.add_argument("--renderdoc-frame")
        .scan<'u', uint64_t>()
        .help("Capture zero-based frame with injected RenderDoc");
    if (!parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    if (cli.present<std::string>("--project") && cli.is_used("--model"))
    {
        throw std::invalid_argument("--project and --model select different scene sources");
    }
    if (cli.present<std::string>("--graph") && cli.present<double>("--color-gain"))
    {
        throw std::invalid_argument("Choose --graph or --color-gain");
    }
    const auto benchmark = cli.present<std::string>("--benchmark");
    const auto warmup    = cli.present<uint64_t>("--warmup").value_or(60);
    const auto samples   = cli.present<uint64_t>("--samples").value_or(120);
    if (benchmark && (benchmark->empty() || samples == 0 || warmup > UINT64_MAX - samples ||
                      !cli.present<std::string>("--revision") || cli.present<std::string>("--revision")->empty() ||
                      cli.present<std::string>("--dump") || cli.present<std::string>("--dump-intermediates") ||
                      cli.get<bool>("--preview-intermediates") || cli.present<uint64_t>("--renderdoc-frame") ||
                      cli.present<uint64_t>("--frames")))
    {
        throw std::invalid_argument("Benchmark requires --revision and positive samples; it cannot combine with "
                                    "--frames, --dump, --dump-intermediates or RenderDoc capture");
    }
    if (!benchmark && (cli.present<uint64_t>("--warmup") || cli.present<uint64_t>("--samples") ||
                       cli.present<std::string>("--revision")))
    {
        throw std::invalid_argument("--warmup, --samples and --revision require --benchmark");
    }
    if (const auto intermediates = cli.present<std::string>("--dump-intermediates");
        intermediates && (intermediates->empty() || std::filesystem::exists(*intermediates)))
    {
        throw std::invalid_argument("Use a fresh nonempty intermediate dump directory");
    }
    if (benchmark && std::filesystem::exists(*benchmark))
    {
        throw std::invalid_argument("Benchmark output directory already exists");
    }
    ResearchApp app(cli);
    app.run(benchmark ? warmup + samples : cli.present<uint64_t>("--frames").value_or(0));
    app.saveResults();
    Logger::app().info("Rendered {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    Logger::app().error("{}", error.what());
    return 1;
}
