#include "../../examples/research/color_gain.hpp"
#include "graph_editor.hpp"
#include "research_workspace.hpp"
#include "scene_inspector.hpp"

#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/profiling/profiler.hpp>
#include <vultra/main/app/imgui_app.hpp>
#include <vultra/main/packaged_resources.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/servers/rendering/texture_blit.hpp>

#include <algorithm>
#include <array>
#include <chrono>

namespace
{
    using namespace vultra;

    constexpr unsigned char kGainShader[] = {
#include "color_gain.slang.h"
    };

    uint64_t researchFeatures(const ResearchDocument& document)
    {
        return document.settings.path == RenderPath::eReferencePathTracing ? VriFeature_RayQuery | VriFeature_Bindless :
                                                                             0;
    }

    class ResearchPanel
    {
    public:
        ResearchPanel(Device&               device,
                      RenderingServer&      server,
                      PassCatalog&          catalog,
                      EditorGui&            gui,
                      ResearchDocument      document,
                      std::filesystem::path launchDirectory,
                      std::string           workspaceFile,
                      std::string           graphFile) :
            m_Device(device),
            m_Gui(gui),
            m_Catalog(catalog),
            m_LaunchDirectory(std::move(launchDirectory)),
            m_Workspace(device, server, catalog),
            m_GraphEditor(catalog),
            m_SceneInspector(gui),
            m_Profiler(device),
            m_PreviewBlit(device, VriFormat_RGBA8_UNORM),
            m_Draft(std::move(document)),
            m_WorkspaceFile(std::move(workspaceFile)),
            m_GraphDefinitionFile(std::move(graphFile))
        {
            m_Catalog.add(research::colorGainDefinition());
            m_Workspace.replace(m_Draft);
            resetDraft();
            m_ExportDirectory =
                "captures/research-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        }

        ~ResearchPanel()
        {
            forgetPreviews(m_Workspace.graph());
            if (m_PreviewTexture)
            {
                m_Gui.forgetTexture(*m_PreviewTexture);
            }
        }

        void exportImages(const std::filesystem::path& directory)
        {
            m_Workspace.exportImages(directory, m_Profiler.timings());
            saveDocument(directory / "workspace.vworkspace");
        }

    private:
        std::filesystem::path path(const std::string& value) const
        {
            if (value.empty())
            {
                throw std::invalid_argument("A file or directory path is required");
            }
            return (m_LaunchDirectory / value).lexically_normal();
        }

        void forgetPreviews(ResearchGraph& graph)
        {
            m_PreviewSource  = nullptr;
            m_ProbeRequested = false;
            m_ProbeValue.reset();
            for (const auto& preview : graph.previews)
            {
                if (graph.graph.resourceInfo(preview.resource).isTexture)
                {
                    m_Gui.forgetTexture(graph.graph.getTexture(preview.resource));
                }
            }
        }

        void resetDraft()
        {
            m_Draft       = m_Workspace.document();
            m_ProjectFile = m_Draft.project.generic_string();
            m_OutputIndex = 0;
        }

    public:
        void applyPending()
        {
            // Both frontends complete the previous frame before applying edits or forgetting previews.
            try
            {
                if (m_LoadWorkspace)
                {
                    const auto candidate = ResearchDocument::load(path(m_WorkspaceFile));
                    m_Workspace.replace(candidate,
                                        [this](auto& old)
                                        {
                                            forgetPreviews(old);
                                        });
                    resetDraft();
                    m_GraphEditor.reset();
                    m_Status = "Opened workspace";
                }
                else if (m_Apply)
                {
                    m_Draft.project = path(m_ProjectFile);
                    if (m_Draft.project == m_Workspace.document().project)
                    {
                        m_Draft.camera        = m_Workspace.camera();
                        m_Draft.sceneSnapshot = m_Workspace.scene().serialize();
                    }
                    else
                    {
                        m_Draft.camera.reset();
                        m_Draft.sceneSnapshot.clear();
                    }
                    m_Workspace.replace(m_Draft,
                                        [this](auto& old)
                                        {
                                            forgetPreviews(old);
                                        });
                    resetDraft();
                    m_Status = "Applied compiled graph";
                }
                m_SceneInspector.applyPending(m_Workspace);
            }
            catch (const std::exception& error)
            {
                m_Status = error.what();
                Logger::app().error("Research edit rejected; active graph retained: {}", error.what());
            }
            selectPreview();
            m_Apply         = false;
            m_LoadWorkspace = false;
        }

    private:
        void saveDocument(const std::filesystem::path& file)
        {
            auto document          = m_Workspace.document();
            document.camera        = m_Workspace.camera();
            document.sceneSnapshot = m_Workspace.scene().serialize();
            m_GraphEditor.saveLayout(document);
            document.save(file);
        }

        void selectPreview()
        {
            const auto& previews = m_Workspace.graph().previews;
            for (size_t i = 0; i < previews.size(); ++i)
            {
                if (previews[i].name == m_Preview)
                {
                    m_OutputIndex = i;
                    m_Preview.clear();
                    break;
                }
            }
        }

    public:
        void draw()
        {
            auto ui = m_Gui.frame();
            ui.setNextWindowPos({12, 40}, ImGuiCond_FirstUseEver);
            ui.setNextWindowSize({320, 460}, ImGuiCond_FirstUseEver);
            {
                EditorGuiWindow window(ui, "Research document");
                if (window)
                {
                    ui.inputText("Project (.vproject)", &m_ProjectFile);
                    int         pathIndex = int(m_Draft.settings.path);
                    const char* paths     = m_Device.core.GetDeviceDesc(m_Device.handle)->hasRayQuery ?
                                                "NaiveDeferred\0NaiveForward\0Reference path tracing\0" :
                                                "NaiveDeferred\0NaiveForward\0";
                    if (ui.combo("Render path", &pathIndex, paths))
                    {
                        m_Draft.settings.path = RenderPath(pathIndex);
                    }
                    constexpr std::array<Extent, 3> sizes {{{640, 360}, {1280, 720}, {1920, 1080}}};
                    constexpr char                  presetItems[] = "640 x 360\0 1280 x 720\0 1920 x 1080\0";
                    int         resolution = int(std::ranges::find(sizes, m_Draft.size) - sizes.begin());
                    std::string items(presetItems, sizeof(presetItems) - 1);
                    if (resolution == int(sizes.size()))
                    {
                        items += std::to_string(m_Draft.size.width) + " x " + std::to_string(m_Draft.size.height) +
                                 " (custom)";
                        items.push_back('\0');
                    }
                    if (ui.combo("Render extent", &resolution, items.c_str()) && resolution < int(sizes.size()))
                    {
                        m_Draft.size = sizes[resolution];
                    }
                    ui.sliderFloat("Exposure", &m_Draft.settings.exposure, -4, 4);
                    ui.beginDisabled(m_Workspace.scene().usesSceneLighting());
                    ui.sliderFloat("Sun intensity", &m_Draft.settings.lightIntensity, 0, 10);
                    ui.endDisabled();
                    if (m_Workspace.scene().usesSceneLighting())
                    {
                        ui.textDisabled("Direct lighting comes from scene nodes");
                    }
                    if (m_Workspace.scene().currentCamera().value != 0)
                    {
                        ui.textDisabled("Using the scene camera");
                    }
                    const bool reference = m_Draft.settings.path == RenderPath::eReferencePathTracing;
                    ui.beginDisabled(reference);
                    ui.checkbox("Skybox", &m_Draft.settings.skybox);
                    ui.checkbox("IBL", &m_Draft.settings.ibl);
                    ui.endDisabled();
                    if (reference)
                    {
                        ui.textDisabled("Reference traces the environment directly");
                    }
                    m_Apply |= ui.button("Apply scene / retry graph");
                    ui.sameLine();
                    if (ui.button("Discard draft"))
                    {
                        resetDraft();
                        m_GraphEditor.reset();
                    }
                    ui.textWrapped("%s", m_Status.c_str());
                    const auto shaderDiagnostics = m_Workspace.shaderDiagnostics();
                    if (!shaderDiagnostics.empty())
                    {
                        ui.textWrapped("%s", shaderDiagnostics.c_str());
                    }
                    ui.textWrapped("Pass parameters preview while dragging. Connections compile after release. "
                                   "Invalid edits keep the active image. Scene settings use Apply.");
                    try
                    {
                        ui.inputText("Graph (.vgraph)", &m_GraphDefinitionFile);
                        if (ui.button("Load graph draft"))
                        {
                            auto definition = GraphDefinition::load(path(m_GraphDefinitionFile));
                            for (const auto& pass : definition.passes)
                            {
                                m_Catalog.definition(pass.type);
                            }
                            m_Draft.definition = std::move(definition);
                            m_Draft.nodePositions.clear();
                            m_GraphEditor.reset();
                            m_Apply  = true;
                            m_Status = "Compiling loaded definition";
                        }
                        ui.sameLine();
                        if (ui.button("Save active graph"))
                        {
                            m_Workspace.document().definition.save(path(m_GraphDefinitionFile));
                            m_Status = "Saved active definition";
                        }
                        ui.inputText("Workspace (.vworkspace)", &m_WorkspaceFile);
                        m_LoadWorkspace = ui.button("Open workspace");
                        ui.sameLine();
                        if (ui.button("Save active workspace"))
                        {
                            saveDocument(path(m_WorkspaceFile));
                            m_Status = "Saved scene, graph and workspace settings";
                        }
                    }
                    catch (const std::exception& error)
                    {
                        m_Status = error.what();
                        Logger::app().error("Research document operation failed: {}", error.what());
                    }
                }
            }
            ui.setNextWindowPos({12, 512}, ImGuiCond_FirstUseEver);
            ui.setNextWindowSize({320, 448}, ImGuiCond_FirstUseEver);
            {
                EditorGuiWindow window(ui, "Scene inspector");
                if (window)
                {
                    if (ui.beginChild("scene-tree", {0, 120}, ImGuiChildFlags_Borders))
                    {
                        m_SceneInspector.drawTree(ui, m_Workspace.scene());
                    }
                    ui.endChild();
                    try
                    {
                        ui.inputText("Scene copy (.vscene)", &m_SceneFile);
                        if (ui.button("Save scene copy"))
                        {
                            m_Workspace.scene().save(path(m_SceneFile));
                            m_Status = "Saved scene nodes and resources";
                        }
                        m_SceneInspector.drawProperties(ui, m_Workspace);
                    }
                    catch (const std::exception& error)
                    {
                        m_Status = error.what();
                        Logger::app().error("Scene inspector edit rejected: {}", error.what());
                    }
                }
            }
            ui.setNextWindowPos({344, 40}, ImGuiCond_FirstUseEver);
            ui.setNextWindowSize({840, 920}, ImGuiCond_FirstUseEver);
            {
                // The canvas owns navigation; parent scrollbars would resize it and cancel the initial Fit.
                EditorGuiWindow window(ui,
                                       "RenderGraph editor",
                                       nullptr,
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                if (window)
                {
                    const auto edit = m_GraphEditor.draw(ui, m_Draft, m_Status);
                    m_Apply |= edit.changed;
                    for (const auto& id : edit.parameterEdits)
                    {
                        const auto pass = std::ranges::find(m_Draft.definition.passes, id, &GraphPassDesc::id);
                        if (pass == m_Draft.definition.passes.end())
                        {
                            continue;
                        }
                        try
                        {
                            // onPreRender runs after frame completion, before this frame's commands are recorded.
                            m_Workspace.setPassParameters(id, pass->parameters);
                        }
                        catch (const std::exception& error)
                        {
                            m_Status = error.what();
                            Logger::app().error("Research parameter edit rejected; active values retained: {}",
                                                error.what());
                        }
                    }
                    if (!edit.preview.empty())
                    {
                        m_Preview = edit.preview;
                        selectPreview();
                    }
                }
            }
            ui.setNextWindowPos({1196, 40}, ImGuiCond_FirstUseEver);
            ui.setNextWindowSize({390, 470}, ImGuiCond_FirstUseEver);
            {
                EditorGuiWindow window(ui, "Outputs");
                if (window)
                {
                    auto& active = m_Workspace.graph();
                    if (ui.beginCombo("Port", active.previews[m_OutputIndex].name.c_str()))
                    {
                        for (size_t i = 0; i < active.previews.size(); ++i)
                        {
                            if (ui.selectable(active.previews[i].name.c_str(), i == m_OutputIndex))
                            {
                                m_OutputIndex = i;
                            }
                        }
                        ui.endCombo();
                    }
                    const auto resource = active.previews[m_OutputIndex].resource;
                    const auto info     = active.graph.resourceInfo(resource);
                    if (info.isTexture)
                    {
                        auto& texture = active.graph.getTexture(resource);
                        if (!m_PreviewTexture || m_PreviewTexture->desc.width != texture.desc.width ||
                            m_PreviewTexture->desc.height != texture.desc.height)
                        {
                            if (m_PreviewTexture)
                            {
                                m_Gui.forgetTexture(*m_PreviewTexture);
                            }
                            VriTextureDesc desc {};
                            desc.type   = VriTextureType_2D;
                            desc.format = VriFormat_RGBA8_UNORM;
                            desc.usage  = VriTextureUsage_ColorAttachment | VriTextureUsage_ShaderResource |
                                         VriTextureUsage_TransferSrc;
                            desc.width       = texture.desc.width;
                            desc.height      = texture.desc.height;
                            desc.depth       = 1;
                            desc.mipNum      = 1;
                            desc.layerNum    = 1;
                            desc.sampleNum   = 1;
                            m_PreviewTexture = std::make_unique<Texture>(m_Device, desc);
                        }
                        if (m_PreviewSource != &texture)
                        {
                            m_ProbeValue.reset();
                        }
                        m_PreviewSource = &texture;
                        m_PreviewBlit.setSource(0, texture);
                        ui.combo("Channel", &m_ViewChannel, "RGB\0R\0G\0B\0A\0Luminance\0");
                        ImGui::InputFloat2("Range", m_ViewRange.data());
                        const ImageView candidate {ImageChannel(m_ViewChannel), m_ViewRange[0], m_ViewRange[1]};
                        try
                        {
                            validateImageView(candidate);
                            m_ImageView = candidate;
                        }
                        catch (const std::invalid_argument&)
                        {
                            ui.textDisabled("Range must be finite and increasing; retaining the last valid view.");
                        }
                        const auto  available = ui.contentRegionAvail();
                        const float height    = std::max(1.0f, available.y - 150);
                        const float width = std::min(available.x, height * texture.desc.width / texture.desc.height);
                        ui.image(m_Gui.textureId(*m_PreviewTexture),
                                 {width, width * texture.desc.height / texture.desc.width});
                        ui.textDisabled("%ux%u, format %d | display mapping only",
                                        texture.desc.width,
                                        texture.desc.height,
                                        int(texture.desc.format));
                        ImGui::InputInt2("Pixel (x, y)", m_ProbePosition.data());
                        m_ProbeRequested = ui.button("Read raw pixel");
                        if (m_ProbeValue)
                        {
                            ui.text("RGBA: %.9g %.9g %.9g %.9g",
                                    (*m_ProbeValue)[0],
                                    (*m_ProbeValue)[1],
                                    (*m_ProbeValue)[2],
                                    (*m_ProbeValue)[3]);
                        }
                    }
                    else
                    {
                        m_PreviewSource  = nullptr;
                        m_ProbeRequested = false;
                        m_ProbeValue.reset();
                        ui.text("Buffer: %llu bytes", static_cast<unsigned long long>(info.bufferDesc.size));
                    }
                    ui.inputText("Capture directory", &m_ExportDirectory);
                    m_ExportRequested = ui.button("Export PNG + linear PFM");
                }
            }
            ui.setNextWindowPos({1196, 524}, ImGuiCond_FirstUseEver);
            ui.setNextWindowSize({390, 436}, ImGuiCond_FirstUseEver);
            EditorGuiWindow graphWindow(ui, "Compiled graph and timings");
            if (graphWindow)
            {
                for (const auto& timing : m_Profiler.timings())
                {
                    ui.text("%*s%s: CPU %.3f ms, GPU %.3f ms",
                            int(timing.depth * 2),
                            "",
                            timing.name.c_str(),
                            timing.cpuMs,
                            timing.gpuMs);
                }
                for (const auto& pass : m_Workspace.graph().snapshot.passes)
                {
                    ui.text("%s %s", pass.active ? "[active]" : "[culled]", pass.name.c_str());
                }
                ui.separatorText("Resources");
                for (const auto& resource : m_Workspace.graph().snapshot.resources)
                {
                    ui.text("%s: %s%s",
                            resource.name.c_str(),
                            resource.active ? "active" : "culled",
                            resource.exported ? ", exported" : "");
                    if (resource.active)
                    {
                        ui.textDisabled("  allocation %u, uses %u..%u%s",
                                        resource.allocation,
                                        resource.firstUse,
                                        resource.lastUse,
                                        resource.history ? ", history" : "");
                        if (resource.memoryKnown)
                        {
                            ui.textDisabled("  VRI allocation %.1f KiB", double(resource.memoryBytes) / 1024);
                        }
                    }
                }
            }
        }

        void prepare()
        {
            m_Workspace.prepareFrame();
        }

        void record(VriCommandBuffer* cmd)
        {
            m_Workspace.record(cmd, &m_Profiler);
            if (m_PreviewSource && m_PreviewTexture)
            {
                const auto& desc = m_PreviewTexture->desc;
                m_PreviewTexture->transition(cmd,
                                             {VriAccess_ColorAttachmentWrite,
                                              VriLayout_ColorAttachment,
                                              VriPipelineStage_ColorAttachmentOutput});
                const float clear[4] {0, 0, 0, 1};
                beginColorPass(m_Device, cmd, m_PreviewTexture->view(), {desc.width, desc.height}, clear);
                m_Device.core.CmdEndRendering(cmd);
                m_PreviewBlit.draw(cmd, *m_PreviewTexture, {0, 0, desc.width, desc.height}, 0, false, m_ImageView);
                m_PreviewTexture->transition(
                    cmd,
                    {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_FragmentShader});
            }
        }

        OrbitCamera& camera()
        {
            return m_Workspace.camera();
        }

        void complete()
        {
            m_Workspace.completeFrame();
            m_Profiler.collect();
            if (m_ProbeRequested && m_PreviewSource)
            {
                try
                {
                    if (m_ProbePosition[0] < 0 || m_ProbePosition[1] < 0)
                    {
                        throw std::invalid_argument("Pixel coordinates cannot be negative");
                    }
                    m_ProbeValue = imagePixel(readback(m_Device, *m_PreviewSource),
                                              uint32_t(m_ProbePosition[0]),
                                              uint32_t(m_ProbePosition[1]));
                }
                catch (const std::exception& error)
                {
                    m_ProbeValue.reset();
                    m_Status = error.what();
                }
                m_ProbeRequested = false;
            }
            if (m_ExportRequested)
            {
                try
                {
                    exportImages(path(m_ExportDirectory));
                    m_Status = "Exported active outputs";
                }
                catch (const std::exception& error)
                {
                    m_Status = error.what();
                    Logger::app().error("Research export failed: {}", error.what());
                }
                m_ExportRequested = false;
            }
        }

    private:
        Device&                             m_Device;
        EditorGui&                          m_Gui;
        PassCatalog&                        m_Catalog;
        std::filesystem::path               m_LaunchDirectory;
        ResearchWorkspace                   m_Workspace;
        GraphEditor                         m_GraphEditor;
        SceneInspector                      m_SceneInspector;
        Profiler                            m_Profiler;
        TextureBlit                         m_PreviewBlit;
        std::unique_ptr<Texture>            m_PreviewTexture;
        Texture*                            m_PreviewSource = nullptr;
        ImageView                           m_ImageView;
        int                                 m_ViewChannel = 0;
        std::array<float, 2>                m_ViewRange {0, 1};
        std::array<int, 2>                  m_ProbePosition {0, 0};
        std::optional<std::array<float, 4>> m_ProbeValue;
        bool                                m_ProbeRequested = false;
        ResearchDocument                    m_Draft;
        std::string                         m_ProjectFile;
        std::string                         m_WorkspaceFile;
        std::string                         m_GraphDefinitionFile;
        std::string                         m_ExportDirectory;
        std::string                         m_Status;
        std::string                         m_SceneFile = "captures/scene.vscene";
        std::string                         m_Preview;
        size_t                              m_OutputIndex     = 0;
        bool                                m_Apply           = false;
        bool                                m_LoadWorkspace   = false;
        bool                                m_ExportRequested = false;
    };

    void drawWorkbench(Device& device, EditorGui& gui, VriCommandBuffer* cmd, Texture& target)
    {
        gui.copy(cmd);
        const float background[4] {0.025f, 0.025f, 0.025f, 1};
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        beginColorPass(device, cmd, target.view(), {target.desc.width, target.desc.height}, background);
        gui.draw(cmd);
        device.core.CmdEndRendering(cmd);
    }

    class ResearchApp final : public ImGuiApp
    {
    public:
        ResearchApp(ResearchDocument             document,
                    const std::filesystem::path& launchDirectory,
                    bool                         persistLayout,
                    const std::string&           workspaceFile,
                    const std::string&           graphFile) :
            ImGuiApp(
                {.title = "Vultra | Research Workbench", .size = {1600, 1000}, .features = researchFeatures(document)},
                {.multiViewport = false,
                 .persistLayout = persistLayout,
                 .appName       = "vultra-app",
                 .iniFile       = launchDirectory / ".vultra/vultra-app/imgui.ini"}),
            m_Panel(getDevice(),
                    getRenderingServer(),
                    getPassCatalog(),
                    getEditorGui(),
                    std::move(document),
                    launchDirectory,
                    workspaceFile,
                    graphFile)
        {
        }

        void exportImages(const std::filesystem::path& directory)
        {
            m_Panel.exportImages(directory);
        }

    private:
        void onUpdate(float) override
        {
        }

        void onPreRender() override
        {
            m_Panel.applyPending();
            ImGuiApp::onPreRender();
            m_Panel.camera().update(getWindow().input(), getWindow().size(), getEditorGui().inputCapture());
            m_Panel.prepare();
        }

        void onImGui() override
        {
            m_Panel.draw();
        }

        void onRender(VriCommandBuffer* cmd, Texture& target) override
        {
            m_Panel.record(cmd);
            drawWorkbench(getDevice(), getEditorGui(), cmd, target);
        }

        void onPostRender(Texture&) override
        {
            m_Panel.complete();
        }

        ResearchPanel m_Panel;
    };

    void renderOffline(ResearchDocument             document,
                       const std::filesystem::path& launchDirectory,
                       const std::string&           workspaceFile,
                       const std::string&           graphFile,
                       uint64_t                     frames,
                       const std::filesystem::path& output)
    {
        Device          device(true, nullptr, researchFeatures(document));
        RenderingServer server(device);
        PassCatalog     catalog(device);
        EditorGui       gui(device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false});
        ResearchPanel
            panel(device, server, catalog, gui, std::move(document), launchDirectory, workspaceFile, graphFile);
        const Extent extent {1600, 1000};
        Texture      target(device, colorTexture(extent));
        Frame        frame(device);
        for (uint64_t i = 0; i < frames; ++i)
        {
            panel.applyPending();
            gui.begin(extent, 1.0f / 60);
            panel.draw();
            gui.upload(extent);
            panel.prepare();
            auto* cmd = frame.begin();
            panel.record(cmd);
            drawWorkbench(device, gui, cmd, target);
            frame.submitAndWait();
            server.collectCompletedFrame();
            panel.complete();
        }
        panel.exportImages(output);
        savePng(readback(device, target), output / "workbench.png");
        Logger::app().info("Offline workbench rendered {} frames into {}", frames, output.string());
    }
} // namespace

int main(int argc, char** argv)
try
{
    using namespace vultra;
    argparse::ArgumentParser cli("vultra-app", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Research workbench for static projects and version-1 RenderGraph graph definitions");
    addAppOptions(cli);
    cli.add_argument("--offline").flag().help("Render the same UI offscreen; requires --frames and --export");
    cli.add_argument("--workspace").help("Open a saved .vworkspace document");
    cli.add_argument("--project").default_value(std::string("resources/research.vproject"));
    cli.add_argument("--graph").default_value(std::string("examples/research/color_gain.vgraph"));
    cli.add_argument("--path").choices("deferred", "forward", "reference").help("Override the document render path");
    cli.add_argument("--seed").scan<'u', uint32_t>().help("Override the reference transport seed");
    cli.add_argument("--export").help("Export active images to a new directory after a finite run");
    if (!parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    if (cli.is_used("--workspace") && (cli.is_used("--project") || cli.is_used("--graph")))
    {
        throw std::invalid_argument("Choose --workspace or --project/--graph");
    }
    const bool offline = cli.get<bool>("--offline");
    const auto frames  = cli.present<uint64_t>("--frames").value_or(0);
    const auto output  = cli.present<std::string>("--export");
    if (output && (frames == 0 || output->empty() || std::filesystem::exists(*output)))
    {
        throw std::invalid_argument("--export requires positive --frames and a new output directory");
    }
    if (offline && (!output || frames == 0))
    {
        throw std::invalid_argument("--offline requires positive --frames and --export");
    }
    const auto       launchDirectory = std::filesystem::current_path();
    const auto       workspaceFile   = cli.present<std::string>("--workspace").value_or("research.vworkspace");
    ResearchDocument document;
    if (cli.is_used("--workspace"))
    {
        document = ResearchDocument::load(workspaceFile);
    }
    else
    {
        document.project    = std::filesystem::absolute(cli.get<std::string>("--project"));
        document.definition = GraphDefinition::load(cli.get<std::string>("--graph"));
    }
    if (const auto path = cli.present<std::string>("--path"))
    {
        document.settings.path = RenderPath::eNaiveDeferred;
        if (*path == "reference")
        {
            document.settings.path = RenderPath::eReferencePathTracing;
        }
        else if (*path == "forward")
        {
            document.settings.path = RenderPath::eNaiveForward;
        }
    }
    if (const auto seed = cli.present<uint32_t>("--seed"))
    {
        document.seed = *seed;
    }
    PackagedResources resources;
    const auto        shader = resources.engineRoot() / "examples/research/shaders/color_gain.slang";
    std::filesystem::create_directories(shader.parent_path());
    static_assert(kGainShader[std::size(kGainShader) - 1] == 0);
    writeFileAtomically(shader, std::as_bytes(std::span(kGainShader).first(std::size(kGainShader) - 1)));
    ScopedWorkingDirectory cwd(resources.engineRoot());
    if (offline)
    {
        renderOffline(std::move(document),
                      launchDirectory,
                      workspaceFile,
                      cli.get<std::string>("--graph"),
                      frames,
                      launchDirectory / *output);
    }
    else
    {
        ResearchApp app(std::move(document),
                        launchDirectory,
                        frames == 0,
                        workspaceFile,
                        cli.get<std::string>("--graph"));
        app.run(frames);
        if (output)
        {
            app.exportImages(launchDirectory / *output);
        }
    }
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
