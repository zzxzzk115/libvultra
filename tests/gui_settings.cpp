#include <vultra/api/ui_bridge.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/process.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>

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

    // Both apps intentionally have the same title and panel name. Only AppName differs.
    void layoutSession(vultra::Device&                device,
                       vultra::Window&                window,
                       const vultra::EditorGuiConfig& config,
                       ImVec2                         position,
                       bool                           restore,
                       bool                           dock = false)
    {
        vultra::EditorGui gui(device, window, VriFormat_BGRA8_UNORM, config);
        for (int frame = 0; frame < 3; ++frame)
        {
            window.poll();
            gui.begin();
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
            ImGui::End();
            gui.upload(window.framebufferSize());
            require(vultra::uiApi().text(uiFrame, "stale", 5) == VULTRA_STATUS_INVALID_FRAME,
                    "UI ABI accepted a frame after upload");
        }
        // Destruction must save even though the normal ImGui autosave interval has not elapsed.
    }
} // namespace

int main()
try
{
    RunDirectory            directory;
    vultra::Window          window("Shared application title", {640, 480});
    vultra::Device          device;
    vultra::EditorGuiConfig config;
    config.multiViewport = false;
    config.appName       = "app-a";
    const auto fileA     = directory.root / ".vultra/app-a/imgui.ini";
    layoutSession(device, window, config, {42, 57}, false);
    const auto originalA = readFile(fileA);
    config.appName       = "app-b";
    layoutSession(device, window, config, {91, 106}, false);
    const auto fileB     = directory.root / ".vultra/app-b/imgui.ini";
    const auto originalB = readFile(fileB);
    require(readFile(fileA) == originalA, "App B changed App A's settings");
    config.appName = "app-a";
    layoutSession(device, window, config, {42, 57}, true);
    require(readFile(fileB) == originalB, "App A changed App B's settings");
    config.appName = "app-b";
    layoutSession(device, window, config, {91, 106}, true);

    config.appName = "docked-app";
    layoutSession(device, window, config, {}, false, true);
    layoutSession(device, window, config, {}, true, true);

    config.appName.clear();
    const auto defaultFile = directory.root / ".vultra" / vultra::executablePath().stem() / "imgui.ini";
    window.setTitle("Different title does not change AppName");
    layoutSession(device, window, config, {30, 40}, false);
    require(std::filesystem::is_regular_file(defaultFile), "Default AppName is not the executable name");
    require(!std::filesystem::exists("imgui.ini"), "GUI wrote the shared run-directory imgui.ini");

    config.iniFile = "custom/nested/layout.ini";
    layoutSession(device, window, config, {33, 44}, false);
    const auto customFile  = directory.root / config.iniFile;
    const auto customSaved = readFile(customFile);
    config.persistLayout   = false;
    layoutSession(device, window, config, {11, 22}, false);
    require(readFile(customFile) == customSaved, "Disabled persistence overwrote the custom layout");
    config.iniFile = "disabled/layout.ini";
    {
        vultra::EditorGui gui(device, window, VriFormat_BGRA8_UNORM, config);
        require(ImGui::GetIO().IniFilename == nullptr, "Disabled persistence still enables loading/saving");
    }
    require(!std::filesystem::exists("disabled"), "Disabled persistence created a directory");

    config.persistLayout = true;
    config.iniFile.clear();
    config.appName = "../escape";
    bool rejected  = false;
    try
    {
        vultra::EditorGui gui(device, window, VriFormat_BGRA8_UNORM, config);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "AppName accepted a directory traversal");
    const std::u8string unicodeName = u8"\u7814\u7a76";
    config.appName.assign(unicodeName.begin(), unicodeName.end());
    layoutSession(device, window, config, {45, 56}, false);
    require(std::filesystem::is_regular_file(directory.root / ".vultra" / unicodeName / "imgui.ini"),
            "Unicode AppName did not save its layout");
    layoutSession(device, window, config, {45, 56}, true);
    {
        vultra::EditorGui gui(device, window, VriFormat_BGRA8_UNORM, {.multiViewport = false, .persistLayout = false});
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
    {
        vultra::EditorGui gui(device, window, VriFormat_BGRA8_UNORM, {.persistLayout = false});
        gui.setTheme(vultra::EditorGuiTheme::eGodot);
        require(ImGui::GetStyle().WindowRounding == 0 && ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w == 1,
                "Theme switching broke platform viewport styling");
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
