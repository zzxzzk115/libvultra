#include <vultra/api/ui_bridge.hpp>
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

extern "C" int testUiAbi(const VultraUiApi* api, VultraUiFrame frame);

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

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

    std::unique_ptr<vultra::EditorGui>
    makeGui(vultra::Device& device, vultra::Window* window, const vultra::EditorGuiConfig& config)
    {
        if (window)
        {
            return std::make_unique<vultra::EditorGui>(device, *window, VriFormat_BGRA8_UNORM, config);
        }
        return std::make_unique<vultra::EditorGui>(device, VriFormat_BGRA8_UNORM, config);
    }

    void layoutSession(vultra::Device&                device,
                       vultra::Window*                window,
                       const vultra::EditorGuiConfig& config,
                       ImVec2                         position,
                       bool                           restore,
                       bool                           dock = false)
    {
        auto  owner = makeGui(device, window, config);
        auto& gui   = *owner;
        for (int frame = 0; frame < 3; ++frame)
        {
            if (window)
            {
                window->poll();
                gui.begin();
            }
            else
            {
                gui.begin({640, 480}, 1.0f / 60);
            }
            const auto uiFrame = vultra::makeUiFrame(gui);
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
            const auto failedLine = testUiAbi(&vultra::uiApi(), uiFrame);
            if (failedLine != 0)
            {
                throw std::runtime_error("C UI ABI failed at line " + std::to_string(failedLine));
            }
            ImGui::End();
            gui.upload(window ? window->framebufferSize() : vultra::Extent {640, 480});
            require(vultra::uiApi().text(uiFrame, "stale", 5) == VULTRA_STATUS_INVALID_FRAME,
                    "UI ABI accepted a frame after upload");
        }
        // Destruction must save even though the normal ImGui autosave interval has not elapsed.
    }
} // namespace

int main(int argc, char** argv)
try
{
    const bool offline = argc == 2 && std::string_view(argv[1]) == "--offline";
    require(argc == 1 || offline, "Usage: test-gui-settings [--offline]");
    RunDirectory                    directory;
    std::unique_ptr<vultra::Window> window;
    if (!offline)
    {
        window = std::make_unique<vultra::Window>("Shared application title", vultra::Extent {640, 480});
    }
    vultra::Device          device;
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
    const auto defaultFile = directory.root / ".vultra" / vultra::executablePath().stem() / "imgui.ini";
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
        auto gui = makeGui(device, window.get(), config);
        require(ImGui::GetIO().IniFilename == nullptr, "Disabled persistence still enables loading/saving");
    }
    require(!std::filesystem::exists("disabled"), "Disabled persistence created a directory");

    config.persistLayout = true;
    config.iniFile.clear();
    config.appName = "../escape";
    bool rejected  = false;
    try
    {
        auto gui = makeGui(device, window.get(), config);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "AppName accepted a directory traversal");
    const std::u8string unicodeName = u8"\u7814\u7a76";
    config.appName.assign(unicodeName.begin(), unicodeName.end());
    layoutSession(device, window.get(), config, {45, 56}, false);
    require(std::filesystem::is_regular_file(directory.root / ".vultra" / unicodeName / "imgui.ini"),
            "Unicode AppName did not save its layout");
    layoutSession(device, window.get(), config, {45, 56}, true);
    {
        auto  owner = makeGui(device, window.get(), {.multiViewport = false, .persistLayout = false});
        auto& gui   = *owner;
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
        vultra::EditorGui gui(device, *window, VriFormat_BGRA8_UNORM, {.persistLayout = false});
        gui.setTheme(vultra::EditorGuiTheme::eGodot);
        require(ImGui::GetStyle().WindowRounding == 0 && ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w == 1,
                "Theme switching broke platform viewport styling");
    }
    else
    {
        bool detachedRejected = false;
        try
        {
            auto gui = makeGui(device, nullptr, {.persistLayout = false});
        }
        catch (const std::invalid_argument&)
        {
            detachedRejected = true;
        }
        require(detachedRejected, "Offscreen GUI accepted detached native windows");
    }
    vultra::Logger::app().info("GUI settings tests passed: isolated apps, restore, docking, executable default, custom "
                               "path, disabled persistence and Unicode");
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
