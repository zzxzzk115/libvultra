#include "support/check.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/process.hpp>
#include <vultra/platform/os/window.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <string_view>

namespace
{
    using support::must;
    using support::require;

    std::string readFile(const std::filesystem::path& file)
    {
        std::ifstream stream(file);
        require(bool(stream), "Layout file was not saved");
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    struct RunDirectory
    {
        std::filesystem::path original = std::filesystem::current_path();
        std::filesystem::path root     = original / "build/.tmp/gui-settings" /
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

        RunDirectory()
        {
            std::filesystem::create_directories(root);
            std::filesystem::current_path(root);
        }

        ~RunDirectory()
        {
            std::filesystem::current_path(original);
        }
    };

    std::expected<std::unique_ptr<vultra::EditorGui>, vultra::Error>
    makeGui(vultra::Device& device, vultra::Window* window, const vultra::EditorGuiConfig& config)
    {
        if (window)
        {
            return vultra::EditorGui::create(device, *window, VriFormat_BGRA8_UNORM, config);
        }
        return vultra::EditorGui::create(device, VriFormat_BGRA8_UNORM, config);
    }

    void layoutSession(vultra::Device&                device,
                       vultra::Window*                window,
                       const vultra::EditorGuiConfig& config,
                       ImVec2                         position,
                       bool                           restore,
                       bool                           dock = false)
    {
        const auto owner = must(makeGui(device, window, config));
        auto&      gui   = *owner;
        for (int frame = 0; frame < 3; ++frame)
        {
            if (window)
            {
                window->poll();
                must(gui.begin());
            }
            else
            {
                must(gui.begin({640, 480}, 1.0f / 60));
            }
            if (!dock)
            {
                ImGui::SetNextWindowPos(restore ? ImVec2(5, 5) : position,
                                        restore ? ImGuiCond_FirstUseEver : ImGuiCond_Always);
            }
            ImGui::SetNextWindowSize({240, 160}, ImGuiCond_FirstUseEver);
            if (dock && !restore)
            {
                ImGui::SetNextWindowDockID(gui.dockspaceId(), ImGuiCond_Always);
            }
            ImGui::Begin("Shared panel");
            if (restore && frame == 2)
            {
                if (dock)
                {
                    require(ImGui::IsWindowDocked(), "Docking layout was not restored");
                }
                else
                {
                    const auto loaded = ImGui::GetWindowPos();
                    require(loaded.x == position.x && loaded.y == position.y, "App loaded another app's layout");
                }
            }
            ImGui::TextUnformatted("Persist this panel");
            ImGui::End();
            must(gui.upload(window ? window->framebufferSize() : vultra::Extent {640, 480}));
        }
        // Destruction must save even though the normal ImGui autosave interval has not elapsed.
    }
} // namespace

int main(int argc, char** argv)
{
    const bool offline = argc == 2 && std::string_view(argv[1]) == "--offline";
    require(argc == 1 || offline, "Usage: test-gui-settings [--offline]");
    RunDirectory                    directory;
    std::unique_ptr<vultra::Window> window;
    if (!offline)
    {
        window = must(vultra::Window::create("Shared application title", vultra::Extent {640, 480}));
    }
    const auto              devicePointer = must(vultra::Device::create());
    auto&                   device        = *devicePointer;
    vultra::EditorGuiConfig config;
    config.multiViewport = false;
    config.appName       = "app-a";
    const auto fileA     = directory.root / ".vultra/app-a/imgui.ini";
    layoutSession(device, window.get(), config, {42, 57}, false);
    const auto originalA = readFile(fileA);
    config.appName       = "app-b";
    layoutSession(device, window.get(), config, {91, 106}, false);
    const auto fileB     = directory.root / ".vultra/app-b/imgui.ini";
    const auto originalB = readFile(fileB);
    require(readFile(fileA) == originalA, "App B changed App A's settings");
    config.appName = "app-a";
    layoutSession(device, window.get(), config, {42, 57}, true);
    require(readFile(fileB) == originalB, "App A changed App B's settings");
    config.appName = "app-b";
    layoutSession(device, window.get(), config, {91, 106}, true);

    config.appName = "docked-app";
    layoutSession(device, window.get(), config, {}, false, true);
    layoutSession(device, window.get(), config, {}, true, true);

    config.appName.clear();
    const auto defaultFile = directory.root / ".vultra" / must(vultra::executablePath()).stem() / "imgui.ini";
    if (window)
    {
        window->setTitle("Different title does not change AppName");
    }
    layoutSession(device, window.get(), config, {30, 40}, false);
    require(std::filesystem::is_regular_file(defaultFile), "Default AppName is not the executable name");
    require(!std::filesystem::exists("imgui.ini"), "GUI wrote the shared run-directory imgui.ini");

    config.iniFile = "custom/nested/layout.ini";
    layoutSession(device, window.get(), config, {33, 44}, false);
    const auto customFile  = directory.root / config.iniFile;
    const auto customSaved = readFile(customFile);
    config.persistLayout   = false;
    layoutSession(device, window.get(), config, {11, 22}, false);
    require(readFile(customFile) == customSaved, "Disabled persistence overwrote the custom layout");
    config.iniFile = "disabled/layout.ini";
    {
        const auto gui = must(makeGui(device, window.get(), config));
        require(ImGui::GetIO().IniFilename == nullptr, "Disabled persistence still enables loading/saving");
    }
    require(!std::filesystem::exists("disabled"), "Disabled persistence created a directory");

    config.persistLayout = true;
    config.iniFile.clear();
    config.appName = "../escape";
    support::rejects(makeGui(device, window.get(), config), "AppName accepted a directory traversal");
    const std::u8string unicodeName = u8"\u7814\u7a76";
    config.appName.assign(unicodeName.begin(), unicodeName.end());
    layoutSession(device, window.get(), config, {45, 56}, false);
    require(std::filesystem::is_regular_file(directory.root / ".vultra" / unicodeName / "imgui.ini"),
            "Unicode AppName did not save its layout");
    layoutSession(device, window.get(), config, {45, 56}, true);
    {
        const auto owner = must(makeGui(device, window.get(), {.multiViewport = false, .persistLayout = false}));
        auto&      gui   = *owner;
        require(bool(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures),
                "GUI renderer cannot bake fonts at display DPI");
        require(ImGui::GetIO().ConfigDpiScaleFonts && ImGui::GetIO().ConfigDpiScaleViewports,
                "GUI did not enable monitor DPI handling");
        require(gui.theme() == vultra::EditorGuiTheme::eUnreal, "Default GUI theme is not Unreal");
        require(std::abs(ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x - 0.082f) < 0.001f,
                "Unreal palette was not applied");
        ImGui::GetStyle().FramePadding = {11, 13};
        gui.setTheme(vultra::EditorGuiTheme::eLight);
        gui.setTheme(vultra::EditorGuiTheme::eUnreal);
        gui.setTheme(vultra::EditorGuiTheme::eDark);
        require(ImGui::GetStyle().FramePadding.x == 11 && ImGui::GetStyle().FramePadding.y == 13,
                "Theme switching reset custom layout spacing");
        require(ImGui::GetStyle().WindowRounding == ImGuiStyle().WindowRounding,
                "Stock theme retained editor-theme rounding");
    }
    if (window)
    {
        const auto owner =
            must(vultra::EditorGui::create(device, *window, VriFormat_BGRA8_UNORM, {.persistLayout = false}));
        auto& gui = *owner;
        gui.setTheme(vultra::EditorGuiTheme::eGodot);
        require(ImGui::GetStyle().WindowRounding == 0 && ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w == 1,
                "Theme switching broke platform viewport styling");
        ImGui::GetIO().ConfigViewportsNoAutoMerge = true;
        for (int index = 0; index < 4; ++index)
        {
            window->poll();
            must(gui.begin());
            const auto mainPosition = ImGui::GetMainViewport()->Pos;
            ImGui::SetNextWindowPos({mainPosition.x + 50, mainPosition.y + 50}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({320, 200}, ImGuiCond_Always);
            ImGui::Begin("Detached DPI regression", nullptr, ImGuiWindowFlags_NoDocking);
            ImGui::TextUnformatted("DPI text and atlas");
            auto* viewport = ImGui::GetWindowViewport();
            ImGui::End();
            must(gui.upload(window->framebufferSize()));
            must(gui.renderPlatformWindows());
            require(viewport != ImGui::GetMainViewport() && viewport->PlatformHandle,
                    "DPI regression did not create a detached platform window");
            const auto actualSize = ImGui::GetPlatformIO().Platform_GetWindowSize(viewport);
            require(std::abs(actualSize.x - 320) <= 1 && std::abs(actualSize.y - 200) <= 1,
                    "Detached window was scaled twice instead of using ImGui's native size");
        }
    }
    else
    {
        support::rejects(makeGui(device, nullptr, {.persistLayout = false}),
                         "Offscreen GUI accepted detached native windows");
    }
    vultra::Logger::app().info("GUI settings tests passed: isolated apps, restore, docking, executable default, custom "
                               "path, disabled persistence and Unicode");
    return 0;
}
