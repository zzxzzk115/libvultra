-- set project name
set_project("VultraEngine")

-- set project version
set_version("0.1.0")

-- set language version: C++ 23
set_languages("cxx23")

option("android_allow_32bit_unsafe")
    set_default(false)
    set_showmenu(true)
    set_description("Allow 32-bit Android builds (unsafe: Vulkan handle truncation risk)")
option_end()

if is_plat("android") then
    set_toolchains("@ndk", {sdkver = "26"})
    if not get_config("android_allow_32bit_unsafe") then
        set_allowedarchs("arm64-v8a")
    end
end

-- root ?
local is_root = (os.projectdir() == os.scriptdir())
set_config("root", is_root)
set_config("project_dir", os.scriptdir())

-- global options
option("vultra_build_examples") -- build examples?
    set_default(not is_plat("android") and not is_plat("wasm"))
    set_showmenu(true)
    set_description("Enable VultraEngine examples")
option_end()

option("vultra_build_tests") -- build tests?
    set_default(not is_plat("android") and not is_plat("wasm"))
    set_showmenu(true)
    set_description("Enable VultraEngine tests")
option_end()

option("vultra_app_validation")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Vulkan validation by default for vultra-app")
option_end()

option("vultra_app_debug_markers")
    set_default(false)
    set_showmenu(true)
    set_description("Enable GPU debug markers by default for vultra-app")
option_end()

option("vultra_app_renderdoc")
    set_default(false)
    set_showmenu(true)
    set_description("Enable RenderDoc integration by default for vultra-app")
option_end()

if is_plat("linux") then
    option("wayland") -- use wayland (Linux only)
        set_default(false)
        set_showmenu(true)
        set_description("Enable Wayland support")
    option_end()
end

if is_plat("windows") then
    add_requireconfs("**", {configs = {runtimes = is_mode("debug") and "MTd" or "MT"}})
end

-- if build on windows
if is_plat("windows") then
    add_cxxflags("/Zc:__cplusplus", {tools = {"msvc", "cl"}}) -- fix __cplusplus == 199711L error
    add_cxxflags("/bigobj") -- avoid big obj
    add_cxxflags("-D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
    add_cxxflags("/EHsc")
    if is_mode("debug") then
        set_runtimes("MTd")
    else
        set_runtimes("MT")
    end
else
    add_cxxflags("-fexceptions")
end

-- add rules
rule("linux.sdl.driver")
    before_run(function (target)
        if is_plat("linux") then
            if has_config("wayland") then
                os.setenv("SDL_VIDEODRIVER", "wayland")
            else
                os.setenv("SDL_VIDEODRIVER", "x11")
            end
        end
    end)
rule_end()

rule("clangd.config")
    on_config(function (target)
        if is_host("windows") then
            os.cp(".clangd.win", ".clangd")
        else
            os.cp(".clangd.nowin", ".clangd")
        end
    end)
rule_end()

rule("imguiconfig")
    set_extensions(".ini")

    on_build_file(function (target, sourcefile, opt) end)

    after_build_file(function (target, sourcefile, opt)
        if path.basename(sourcefile) ~= "imgui" then
            return
        end
        local output_path = path.join(target:targetdir(), path.filename(sourcefile))
        os.cp(sourcefile, output_path)
        print("Copying imgui config: " .. sourcefile .. " -> " .. output_path)
    end)
rule_end()

rule("macos.package_rpath")
    after_load(function (target)
        if not is_plat("macosx") then
            return
        end

        local kind = target:get("kind")
        if kind ~= "binary" and kind ~= "shared" and kind ~= "module" then
            return
        end

        local collected = {}
        local visited = {}

        local function collect_package_libdirs(t)
            if not t then
                return
            end
            local key = t:name()
            if key and visited[key] then
                return
            end
            if key then
                visited[key] = true
            end

            local pkgs = t:orderpkgs()
            if pkgs then
                for _, pkg in ipairs(pkgs) do
                    if pkg and pkg:installdir() then
                        local libdir = path.join(pkg:installdir(), "lib")
                        if os.isdir(libdir) then
                            collected[libdir] = true
                        end
                    end
                end
            end

            local deps = t:orderdeps()
            if deps then
                for _, dep in ipairs(deps) do
                    collect_package_libdirs(dep)
                end
            end
        end

        collect_package_libdirs(target)
        for libdir, _ in pairs(collected) do
            target:add("rpathdirs", libdir)
            target:add("ldflags", "-Wl,-rpath," .. libdir, {force = true})
        end
    end)
rule_end()

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode", lsp = "clangd"})
add_rules("clangd.config", "linux.sdl.driver", "imguiconfig", "macos.package_rpath")

-- add repositories
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")

-- shared rules (defined before any target that uses them)
includes("xmake/rules/wasm.lua")

-- include external libraries
includes("external")

-- bulitin tasks and targets
includes("builtin")

-- include source
includes("source")

-- include tests
if has_config("vultra_build_tests") then
    includes("tests")
end

-- if build examples, then include examples
if has_config("vultra_build_examples") then
    includes("examples")
end

-- global task for running standard examples
task("examples")
    set_menu {
        usage = "xmake examples",
        description = "Run standard examples, without modern features (ray tracing, mesh shaders, etc.).",
        options = {}
    }
    on_run(function ()
        local examples = {
            "window",
            "rhi-triangle",
            "imgui",
            "framegraph-triangle",
            "openxr-triangle",
            "openxr-sponza",
            "gltf-viewer",
            "debugdraw",
            "gaussian-splatting",
        }
        for _, example in ipairs(examples) do
            os.execv("xmake", {"run", "example-" .. example})
        end
    end)
task_end()
