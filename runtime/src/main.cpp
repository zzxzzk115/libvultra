#include <vultra/api/render_settings.generated.hpp>
#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/assets/vpk_archive.hpp>
#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/main/app/imgui_app.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/platform/os/process.hpp>
#include <vultra/scene/camera/orbit_camera.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>
#include <vultra/servers/rendering/research/capture.hpp>
#include <vultra/ui/vgui.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
extern "C" const std::byte vultra_builtin_pack_start[];
extern "C" const std::byte vultra_builtin_pack_end[];
#endif

using namespace vultra;

namespace
{
    std::span<const std::byte> builtinPack()
    {
#if defined(_WIN32)
        const auto  module   = GetModuleHandleW(nullptr);
        const auto  resource = FindResourceW(module, MAKEINTRESOURCEW(101), RT_RCDATA);
        const auto  loaded   = resource ? LoadResource(module, resource) : nullptr;
        const auto* data     = loaded ? static_cast<const std::byte*>(LockResource(loaded)) : nullptr;
        const auto  size     = resource ? SizeofResource(module, resource) : 0;
        if (!data || size == 0)
        {
            throw std::runtime_error("Runtime builtin pack is missing");
        }
        return {data, size};
#else
        const auto* begin = &vultra_builtin_pack_start[0];
        const auto* end   = &vultra_builtin_pack_end[0];
        if (end <= begin)
        {
            throw std::runtime_error("Runtime builtin pack is empty");
        }
        return {begin, size_t(end - begin)};
#endif
    }

    struct ExtractedProject
    {
        explicit ExtractedProject(const VpkArchive& archive) :
            root(std::filesystem::temp_directory_path() / ("vultra-runtime-" + StableId::generate().toString())),
            projectRoot(root / "project"),
            engineRoot(root / "engine")
        {
            if (!archive.contains("project.vproject"))
            {
                throw std::invalid_argument("Project VPK has no project manifest");
            }
            std::filesystem::create_directories(root);
            try
            {
                const auto builtinFile = root / "builtin.vpk";
                writeFileAtomically(builtinFile, builtinPack());
                VpkArchive(builtinFile).extractTo(engineRoot);
                archive.extractTo(projectRoot);
            }
            catch (...)
            {
                std::filesystem::remove_all(root);
                throw;
            }
        }

        ~ExtractedProject()
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }

        std::filesystem::path root;
        std::filesystem::path projectRoot;
        std::filesystem::path engineRoot;
    };

    struct WorkingDirectory
    {
        explicit WorkingDirectory(const std::filesystem::path& path) :
            previous(std::filesystem::current_path())
        {
            std::filesystem::current_path(path);
        }

        ~WorkingDirectory()
        {
            std::error_code error;
            std::filesystem::current_path(previous, error);
        }

        std::filesystem::path previous;
    };

    struct RuntimeProject
    {
        ProjectManifest       manifest;
        SceneTree             scene;
        std::filesystem::path root;
        std::filesystem::path environmentPath;
        std::filesystem::path uiDocumentPath;
        std::filesystem::path uiFontPath;
    };

    RuntimeProject loadProject(const std::filesystem::path& root)
    {
        auto manifest = ProjectManifest::load(root / "project.vproject");
        auto scene    = SceneTree::load(root / manifest.mainScene);
        scene.validateAssets(manifest);
        RuntimeProject project {std::move(manifest), std::move(scene), root, {}, {}, {}};
        if (project.manifest.environment)
        {
            project.environmentPath = root / project.manifest.asset(*project.manifest.environment).path;
        }
        if (project.manifest.uiDocument)
        {
            project.uiDocumentPath = root / project.manifest.asset(*project.manifest.uiDocument).path;
        }
        if (project.manifest.uiFont)
        {
            project.uiFontPath = root / project.manifest.asset(*project.manifest.uiFont).path;
        }
        return project;
    }

    class RuntimeApp final : public ImGuiApp
    {
    public:
        RuntimeApp(RuntimeProject project, std::filesystem::path capture, bool debugUi) :
            ImGuiApp({.title = "Vultra Runtime", .size = {1024, 768}},
                     {.multiViewport = false, .persistLayout = false}),
            m_Project(std::move(project)),
            m_Environment(getDevice(), m_Project.environmentPath),
            m_GpuScene(
                getRenderingServer().uploadScene(importScene(m_Project.scene, m_Project.manifest, m_Project.root))),
            m_Renderer(getDevice(), *m_GpuScene, m_Environment, getSwapchain().format()),
            m_Scripts(m_Project.scene),
            m_Capture(std::move(capture)),
            m_DebugUiEnabled(debugUi),
            m_DebugUiVisible(debugUi)
        {
            if (!m_Project.uiDocumentPath.empty())
            {
                m_VGui = std::make_unique<VGui>(getDevice(), getWindow(), getSwapchain().format());
                if (!m_Project.uiFontPath.empty())
                {
                    m_VGui->loadFont(m_Project.uiFontPath);
                }
                m_VGui->loadDocument(m_Project.uiDocumentPath);
                m_VGui->bindChange(
                    "toggle-skybox",
                    [this]()
                    {
                        m_Renderer.settings.skybox = m_VGui->isChecked("toggle-skybox");
                        m_VGui->setText("status", m_Renderer.settings.skybox ? "Skybox enabled" : "Skybox disabled");
                    });
            }
            m_Camera.center   = m_GpuScene->center;
            m_Camera.radius   = m_GpuScene->radius;
            m_Camera.distance = m_GpuScene->radius * 2.5f;
            m_Camera.pitch    = 0.2f;
            for (const auto& extension : m_Project.manifest.extensions)
            {
                m_Scripts.addExtension(m_Project.root / extension);
            }
            for (const auto& script : m_Project.manifest.scripts)
            {
                m_Scripts.add(script, m_Project.root / script.path);
            }
        }

        void saveCapture()
        {
            if (m_Capture.empty())
            {
                return;
            }
            if (!m_Graph)
            {
                throw std::runtime_error("Runtime ended before rendering a frame");
            }
            savePng(readback(getDevice(), m_Graph->getTexture(m_SceneColor)), m_Capture);
        }

    private:
        void onUpdate(float deltaSeconds) override
        {
            if (m_DebugUiEnabled && getWindow().input().isKeyPressed(KeyCode::eF1))
            {
                m_DebugUiVisible = !m_DebugUiVisible;
            }
            m_Renderer.pollShaders();
            m_Scripts.update(deltaSeconds);
        }

        void onImGui() override
        {
            if (m_DebugUiVisible)
            {
                drawDebugUi();
            }
            m_Scripts.gui(getEditorGui());
        }

        void drawDebugUi()
        {
            auto ui = getEditorGui().frame();
            ui.setNextWindowSize({430, 470}, ImGuiCond_FirstUseEver);
            EditorGuiWindow window(ui, "Vultra Debug");
            if (!window)
            {
                return;
            }
            ui.text("Scene: %s", m_Project.scene.root().name().c_str());
            ui.text("%zu draws", m_GpuScene->primitives.size());
            if (m_LastFrame)
            {
                ui.text("Frame %llu: CPU %.2f ms",
                        static_cast<unsigned long long>(m_LastFrame->frameIndex),
                        m_LastFrame->totalMs);
                if (m_LastFrame->gpuMs)
                {
                    ui.text("GPU %.2f ms", *m_LastFrame->gpuMs);
                }
            }
            if (EditorGuiInspector inspector(getEditorGui(), "Render settings"); inspector)
            {
                drawRenderSettings(inspector, m_Renderer.settings);
            }
            const auto diagnostics = m_Renderer.diagnostics();
            if (!diagnostics.empty())
            {
                ui.textWrapped("%s", diagnostics.c_str());
            }
            if (m_GraphSnapshot)
            {
                ui.separatorText("RenderGraph");
                ui.text("%zu passes, %zu resources", m_GraphSnapshot->passes.size(), m_GraphSnapshot->resources.size());
                for (const auto& pass : m_GraphSnapshot->passes)
                {
                    ui.bulletText("%s %s", pass.active ? "[active]" : "[culled]", pass.name.c_str());
                }
            }
        }

        void onFrameComplete(const FrameTiming& timing) override
        {
            m_LastFrame = timing;
        }

        void onPreRender() override
        {
            ImGuiApp::onPreRender();
            auto capture = getEditorGui().inputCapture();
            if (m_VGui)
            {
                m_VGui->setChecked("toggle-skybox", m_Renderer.settings.skybox);
                m_VGui->update(getWindow().input(), getSwapchain().size());
                const auto gameCapture = m_VGui->inputCapture();
                capture.mouse |= gameCapture.mouse;
                capture.keyboard |= gameCapture.keyboard;
            }
            m_Camera.update(getWindow().input(), getWindow().size(), capture);
        }

        void onRender(VriCommandBuffer* cmd, Texture& target) override
        {
            if (!m_Graph || m_Size != getSwapchain().size() || m_Outputs.path != m_Renderer.settings.path)
            {
                m_Size  = getSwapchain().size();
                m_Graph = std::make_unique<RenderGraph>(getDevice());
                m_GraphSnapshot.reset();
                m_Outputs    = m_Renderer.addPasses(*m_Graph, m_Size);
                m_SceneColor = m_Outputs.color;
                m_Backbuffer = m_Graph->importResource("backbuffer", target, false);
                m_Graph->addPass("Copy scene",
                                 {{m_SceneColor, Usage::eCopySource}, {m_Backbuffer, Usage::eCopyDestination}},
                                 [this](auto* command, auto& resources)
                                 {
                                     VriTextureCopyDesc copy {};
                                     copy.src.layerNum = 1;
                                     copy.dst.layerNum = 1;
                                     copy.src.aspect   = VriImageAspect_Color;
                                     copy.dst.aspect   = VriImageAspect_Color;
                                     getDevice().core.CmdCopyTexture(command,
                                                                     resources.getTexture(m_Backbuffer).handle,
                                                                     resources.getTexture(m_SceneColor).handle,
                                                                     &copy);
                                 });
                if (m_VGui)
                {
                    m_Graph->addPass("Game UI",
                                     {{m_Backbuffer, Usage::eColorReadWrite}},
                                     [this](auto* command, auto& resources)
                                     {
                                         m_VGui->draw(command, resources.getTexture(m_Backbuffer));
                                     });
                }
                m_Graph->addPass(
                    "Plugin UI",
                    {{m_Backbuffer, Usage::eColorReadWrite}},
                    [this](auto* command, auto& resources)
                    {
                        getEditorGui().copy(command);
                        beginColorPass(getDevice(), command, resources.getTexture(m_Backbuffer).view(), m_Size);
                        getEditorGui().draw(command);
                        getDevice().core.CmdEndRendering(command);
                    });
                m_Graph->addPass(
                    "Present",
                    {{m_Backbuffer, Usage::ePresent}},
                    [](auto*, auto&)
                    {
                    },
                    true);
                m_Graph->exportResource(m_SceneColor);
                m_Graph->compile();
                m_GraphSnapshot = m_Graph->snapshot();
            }
            m_Graph->bind(m_Backbuffer, target);
            m_Renderer.prepare(m_Camera.camera(m_Size), *m_Graph, m_Outputs);
            m_Graph->execute(cmd);
        }

        RuntimeProject                       m_Project;
        Environment                          m_Environment;
        GpuSceneHandle                       m_GpuScene;
        BuiltinRenderer                      m_Renderer;
        OrbitCamera                          m_Camera;
        std::unique_ptr<VGui>                m_VGui;
        ScriptHost                           m_Scripts;
        std::filesystem::path                m_Capture;
        std::unique_ptr<RenderGraph>         m_Graph;
        BuiltinRenderer::Outputs             m_Outputs {};
        RenderGraph::Resource                m_SceneColor {};
        RenderGraph::Resource                m_Backbuffer {};
        Extent                               m_Size {};
        std::optional<RenderGraph::Snapshot> m_GraphSnapshot;
        std::optional<FrameTiming>           m_LastFrame;
        bool                                 m_DebugUiEnabled = false;
        bool                                 m_DebugUiVisible = false;
    };
} // namespace

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("vultra-runtime", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Run a packaged Vultra project");
    addAppOptions(cli);
    cli.add_argument("package").default_value(std::string {}).help("Project VPK archive (optional when embedded)");
    cli.add_argument("--capture").help("Save the final scene image");
    cli.add_argument("--debug-ui").flag().help("Show the ImGui renderer and RenderGraph debugger (F1 toggles)");
    if (!parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    const auto capture     = cli.present<std::string>("--capture");
    const auto capturePath = capture ? std::filesystem::absolute(*capture) : std::filesystem::path {};
    const auto package     = cli.get<std::string>("package");
    auto       archive     = package.empty() ? VpkArchive::embeddedProject(executablePath()) :
                                               std::optional<VpkArchive> {VpkArchive(package)};
    if (!archive)
    {
        throw std::invalid_argument("No embedded project VPK; provide a package path");
    }
    ExtractedProject extracted(*archive);
    auto             project = loadProject(extracted.projectRoot);
    WorkingDirectory cwd(extracted.engineRoot);
    RuntimeApp       app(std::move(project), capturePath, cli.get<bool>("--debug-ui"));
    app.run(cli.present<uint64_t>("--frames").value_or(0));
    app.saveCapture();
    Logger::app().info("Runtime rendered {} frames", app.frameCount());
    return 0;
}
catch (const std::exception& error)
{
    Logger::app().error("{}", error.what());
    return 1;
}
