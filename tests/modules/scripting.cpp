#include <vultra/platform/os/file.hpp>
#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>
#ifdef TEST_API_UI
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/ui/editor_gui.hpp>
#endif

#include <array>
#include <chrono>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#ifdef TEST_SCRIPT_LUA
#include <string>
#endif
#include <string_view>

namespace
{
    using namespace vultra;
    using GuiCallback = std::function<void(ScriptHost&)>;

    constexpr float kDelta = 1.0f / 60;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void write(const std::filesystem::path& path, std::string_view text)
    {
        writeFileAtomically(path, std::as_bytes(std::span(text)));
        std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));
    }

    void replace(const std::filesystem::path& source, const std::filesystem::path& target)
    {
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(target, std::filesystem::last_write_time(target) + std::chrono::seconds(2));
    }

    void position(const Node3D& node, float x, float y, float z)
    {
        require(node.localTransform()[3] == glm::vec4(x, y, z, 1), "Script did not publish its expected transform");
    }

    void missingLanguages(const std::filesystem::path& root)
    {
        SceneTree  scene(std::make_unique<Node3D>("Missing language"));
        ScriptHost host(scene);
        const auto path = root / "unselected.script";
        write(path, "Not a module");
        const std::array    languages {ScriptModule::Language::eNative,
                                       ScriptModule::Language::eLua,
                                       ScriptModule::Language::eCSharp};
        const std::array    names {"script_native", "script_lua", "script_csharp"};
        std::array<bool, 3> selected {};
#ifdef TEST_SCRIPT_NATIVE
        selected[0] = true;
#endif
#ifdef TEST_SCRIPT_LUA
        selected[1] = true;
#endif
#ifdef TEST_SCRIPT_CSHARP
        selected[2] = true;
#endif
        for (size_t index = 0; index < languages.size(); ++index)
        {
            if (selected[index])
            {
                continue;
            }
            bool rejected = false;
            try
            {
                host.add({languages[index], path, "Motion", scene.root().idInScene().value}, path);
            }
            catch (const std::invalid_argument& error)
            {
                rejected = std::string_view(error.what()).find(names[index]) != std::string_view::npos;
            }
            require(rejected, "Unselected script language did not identify its required feature");
        }
#ifndef TEST_SCRIPT_NATIVE
        bool rejected = false;
        try
        {
            host.addExtension(path);
        }
        catch (const std::invalid_argument& error)
        {
            rejected = std::string_view(error.what()).find("script_native") != std::string_view::npos;
        }
        require(rejected, "Unselected native extension was accepted");
#endif
    }

#ifdef TEST_SCRIPT_NATIVE
    void nativeScripts(const std::filesystem::path& binary, const std::filesystem::path& root, const GuiCallback& gui)
    {
#ifdef _WIN32
        constexpr auto library     = "module-native-code.dll";
        constexpr auto replacement = "module-native-replacement.dll";
#else
        constexpr auto library     = "libmodule-native-code.so";
        constexpr auto replacement = "libmodule-native-replacement.so";
#endif
        const auto path = root / library;
        replace(binary / library, path);
        SceneTree  scene(std::make_unique<Node>("Native"));
        auto&      first  = static_cast<Node3D&>(scene.addChild(scene.root(), std::make_unique<Node3D>("First")));
        auto&      second = static_cast<Node3D&>(scene.addChild(scene.root(), std::make_unique<Node3D>("Second")));
        ScriptHost host(scene, true);
        host.addExtension(path);
        host.add({ScriptModule::Language::eNative, path, "First", first.idInScene().value}, path);
        host.add({ScriptModule::Language::eNative, path, "Second", second.idInScene().value}, path);
        host.update(kDelta);
        position(first, 1, 0, 0);
        position(second, 2, 0, 0);
        gui(host);
        replace(binary / replacement, path);
        require(host.reloadChanged() == 1 && host.lastReloadError().empty(), "Native provider replacement failed");
        host.update(kDelta);
        position(first, 3, 0, 0);
        position(second, 4, 0, 0);
        gui(host);
        write(path, "Not a native library");
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty(),
                "Invalid native replacement was accepted");
        host.update(kDelta);
        position(first, 3, 0, 0);
        position(second, 4, 0, 0);
        replace(binary / library, path);
        require(host.reloadChanged(true) == 1 && host.lastReloadError().empty(), "Native provider did not recover");
        host.update(kDelta);
        position(first, 1, 0, 0);
        position(second, 2, 0, 0);
        host.stop();
    }
#endif

#ifdef TEST_SCRIPT_LUA
    std::string luaSource(int step)
    {
        return R"lua(local script = {}
function script:_ready()
    local x, y, z = self.node:position()
    self.node:set_position(x + 1, y, z)
end
function script:_physics_process(delta)
    assert(math.abs(delta - 1 / 60) < 1e-7)
    local x, y, z = self.node:position()
    self.node:set_position(x, y + 1, z)
end
function script:_process(delta)
    local x, y, z = self.node:position()
    self.node:set_position(x, y, z + )lua" +
               std::to_string(step) + R"lua()
end
function script:_editor_gui(gui) gui:text("Lua motion") end
return script
)lua";
    }

    void luaScript(const std::filesystem::path& root, const GuiCallback& gui)
    {
        const auto path = root / "motion.lua";
        write(path, luaSource(1));
        SceneTree   scene(std::make_unique<Node3D>("Lua"));
        const auto& node = static_cast<const Node3D&>(scene.root());
        ScriptHost  host(scene, true);
        host.add({ScriptModule::Language::eLua, path, {}, scene.root().idInScene().value}, path);
        host.update(kDelta);
        position(node, 1, 1, 1);
        gui(host);
        write(path, luaSource(2));
        require(host.reloadChanged() == 1 && host.lastReloadError().empty(), "Lua replacement failed");
        host.update(kDelta);
        position(node, 1, 2, 3);
        gui(host);
        write(path, "Invalid Lua code!");
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty(), "Invalid Lua replacement was accepted");
        host.update(kDelta);
        position(node, 1, 3, 5);
        write(path, luaSource(1));
        require(host.reloadChanged(true) == 1 && host.lastReloadError().empty(), "Lua did not recover");
        host.update(kDelta);
        position(node, 1, 4, 6);
        host.stop();
    }
#endif

#ifdef TEST_SCRIPT_CSHARP
    void managedScript(const std::filesystem::path& binary, const std::filesystem::path& root, const GuiCallback& gui)
    {
        const auto directory = root / "managed";
        std::filesystem::copy(binary / "managed", directory, std::filesystem::copy_options::recursive);
        const auto path = directory / "Vultra.ModuleScripts.dll";
        SceneTree  scene(std::make_unique<Node>("Managed"));
        auto&      node = static_cast<Node3D&>(scene.addChild(scene.root(), std::make_unique<Node3D>("Motion")));
        ScriptHost host(scene, true);
        host.add({ScriptModule::Language::eCSharp, path, "VultraModuleTests.Motion", node.idInScene().value}, path);
        host.update(kDelta);
        position(node, 1, 1, 1);
        gui(host);
        replace(directory / "replacement/Vultra.ModuleScripts.dll", path);
        require(host.reloadChanged() == 1 && host.lastReloadError().empty(), "Managed replacement failed");
        host.update(kDelta);
        position(node, 1, 2, 3);
        gui(host);
        write(path, "Not a managed assembly");
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty(),
                "Invalid managed replacement was accepted");
        host.update(kDelta);
        position(node, 1, 3, 5);
        replace(binary / "managed/Vultra.ModuleScripts.dll", path);
        require(host.reloadChanged(true) == 1 && host.lastReloadError().empty(), "Managed script did not recover");
        host.update(kDelta);
        position(node, 1, 4, 6);
        host.stop();
    }
#endif

#ifdef TEST_API_UI
    struct GuiProbe
    {
        Device    device;
        EditorGui gui {device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false}};
        Frame     frame {device};
        Texture   target {device, colorTexture({320, 240})};

        void draw(ScriptHost& host)
        {
            gui.begin({320, 240}, kDelta);
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({320, 240});
            ImGui::Begin("Script callback",
                         nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
            host.gui(gui);
            ImGui::End();
            gui.upload({320, 240});
            auto* command = frame.begin();
            gui.copy(command);
            target.transition(
                command,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const float clear[4] {0, 0, 0, 1};
            beginColorPass(device, command, target.view(), {320, 240}, clear);
            gui.draw(command);
            device.core.CmdEndRendering(command);
            frame.submitAndWait();
            const auto image   = readback(device, target);
            bool       visible = false;
            for (size_t index = 0; index < image.rgba.size(); ++index)
            {
                visible |= index % 4 != 3 && image.rgba[index] > 0.2f;
            }
            require(visible, "Script UI produced an empty image");
        }
    };
#endif
} // namespace

int main(int argc, char** argv)
try
{
    require(argc == 2, "Pass an unused scripting output directory");
#if defined(TEST_SCRIPT_NATIVE) || defined(TEST_SCRIPT_CSHARP)
    const auto binary = std::filesystem::absolute(argv[0]).parent_path();
#endif
    const std::filesystem::path root(argv[1]);
    require(!std::filesystem::exists(root), "Scripting output directory already exists");
    std::filesystem::create_directories(root);
#ifdef TEST_API_UI
    GuiProbe          ui;
    const GuiCallback gui = [&ui](ScriptHost& host)
    {
        ui.draw(host);
    };
#else
    const GuiCallback gui = [](ScriptHost&)
    {
    };
#endif
    missingLanguages(root);
    SceneTree  emptyScene(std::make_unique<Node>("Empty host"));
    ScriptHost emptyHost(emptyScene);
    emptyHost.update(kDelta);
#ifndef TEST_API_UI
    gui(emptyHost);
#endif
    emptyHost.stop();
#ifdef TEST_SCRIPT_NATIVE
    nativeScripts(binary, root, gui);
#endif
#ifdef TEST_SCRIPT_LUA
    luaScript(root, gui);
#endif
#ifdef TEST_SCRIPT_CSHARP
    managedScript(binary, root, gui);
#endif
    std::cout << "Scripting isolation passed: selected languages, CPU logic and replacement/failure recovery";
#ifdef TEST_API_UI
    std::cout << ", offscreen UI readback";
#endif
    std::cout << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
