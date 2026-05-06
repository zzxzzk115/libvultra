#include "app_state.hpp"
#include "editor_app/editor_app.hpp"
#include "launch_options.hpp"
#include "project_launcher/project_launcher.hpp"
#include "vproject.hpp"

#include <vultra/core/app/demo_app_host.hpp>
#include <vultra/core/base/common_context.hpp>
#include <vultra/function/rendering/srp/renderer.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace
{
    class VultraShellRenderer final : public vultra::FeatureRenderer
    {
    public:
        VultraShellRenderer(vultra_app::AppState&        state,
                            vultra_app::ProjectLauncher& launcher,
                            vultra_app::EditorApp&       editor) :
            m_State(state), m_Launcher(launcher), m_Editor(editor)
        {
        }

        std::string_view name() const override { return "vultra_shell"; }

        void onImGui() override
        {
            if (m_State.mode == vultra_app::AppMode::Editor)
            {
                vultra_app::EditorContext ctx {.state = m_State, .services = getServices()};
                m_Editor.draw(ctx);
            }
            else
                m_Launcher.draw(m_State);
        }

    private:
        vultra_app::AppState&        m_State;
        vultra_app::ProjectLauncher& m_Launcher;
        vultra_app::EditorApp&       m_Editor;
    };

    class VultraStandaloneApp final : public vultra::DemoAppHost
    {
    public:
        explicit VultraStandaloneApp(vultra_app::LaunchOptions options) : m_Options(std::move(options))
        {
            m_VpkPath = vultra_app::findDefaultVpk(m_Options);
            if (m_Options.editorMode)
            {
                m_State.mode = vultra_app::AppMode::Editor;
                if (!m_Options.projectPath.empty())
                {
                    m_State.currentProject = m_Options.projectPath;
                    if (auto project = vultra_app::loadVProject(m_Options.projectPath); project.has_value())
                    {
                        m_State.currentProject      = project->projectDir;
                        m_State.currentProjectName  = project->name;
                        m_State.currentAssetRoot    = project->assetRoot;
                        m_State.currentDefaultScene = project->defaultScene;
                    }
                }
            }
            else if (m_VpkPath.has_value())
            {
                m_State.mode = vultra_app::AppMode::Runtime;
            }
            else
            {
                m_State.mode = vultra_app::AppMode::Launcher;
            }
        }

    private:
        std::string_view demoWindowTitle() const override
        {
            if (m_Options.editorMode)
                return vultra_app::EditorApp::kWindowTitle;
            if (!m_VpkPath.has_value())
                return vultra_app::ProjectLauncher::kWindowTitle;
            return "VultraEngine";
        }

        bool demoEnableExperimentalWebGPUContent() const override { return true; }

        vultra::Ref<vultra::Renderer> makeRenderer() const override
        {
            if (m_State.mode == vultra_app::AppMode::Runtime)
                return vultra::DemoAppHost::makeRenderer();

            return vultra::createRef<VultraShellRenderer>(m_State, m_Launcher, m_Editor);
        }

        void onConfigureDemo(vultra::Engine& engine) override
        {
            if (m_State.mode != vultra_app::AppMode::Runtime)
                engine.ctx().config.imgui.enableDocking = true;

            if (m_Options.editorMode)
            {
                vultra_app::EditorApp::configureProject(engine, m_Options);
                return;
            }

            if (m_VpkPath.has_value())
            {
                engine.ctx().config.asset.loadFromVPK = true;
                engine.ctx().config.asset.assetRoot   = "/";
                engine.ctx().config.asset.vpkFile     = m_VpkPath->generic_string();
                return;
            }

            vultra_app::ProjectLauncher::configureAssets(engine, m_Options);
        }

        void onPostConfigureDemo(vultra::Engine& engine) override
        {
            if (m_Options.editorMode)
            {
                vultra_app::EditorApp::logStartup(m_Options);
                return;
            }

            if (!m_VpkPath.has_value())
            {
                vultra_app::ProjectLauncher::logStartup();
                return;
            }

            auto& sceneService = engine.ctx().services.require<vultra::ISceneService>();
            auto& worldService = engine.ctx().services.require<vultra::IWorldService>();
            sceneService.instantiateScene(worldService.world(), m_Options.sceneUri);

            VULTRA_CLIENT_INFO("[Vultra] Loaded scene '{}' from VPK '{}'", m_Options.sceneUri, m_VpkPath->generic_string());
        }

        vultra_app::LaunchOptions             m_Options;
        std::optional<std::filesystem::path>  m_VpkPath;
        mutable vultra_app::AppState          m_State;
        mutable vultra_app::ProjectLauncher   m_Launcher;
        mutable vultra_app::EditorApp         m_Editor;
    };
} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    if (argc > 1 && argv != nullptr)
    {
        args.reserve(static_cast<size_t>(argc - 1));
        for (int i = 1; i < argc; ++i)
        {
            if (argv[i] != nullptr)
                args.emplace_back(argv[i]);
        }
    }

    auto options = vultra_app::parseLaunchOptions(args);
    if (options.showHelp)
    {
        vultra_app::printUsage();
        return 0;
    }
    if (options.cliOnly)
        return vultra_app::runCliOnly(options);

    VultraStandaloneApp app {std::move(options)};
    return app.run(argc, argv);
}
