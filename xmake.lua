-- set project name
set_project("libvultra")

-- set project version
set_version("0.1.0")

-- set language version: C++ 23
set_languages("cxx23")

-- root ?
local is_root = (os.projectdir() == os.scriptdir())
set_config("root", is_root)

-- global options
if is_root then
    option("examples") -- build examples?
        set_default(true)
        set_showmenu(true)
        set_description("Enable examples")
    option_end()

    option("tests") -- build tests?
        set_default(true)
        set_showmenu(true)
        set_description("Enable tests")
    option_end()
else
    set_config("examples", false)
    set_config("tests", false)
end

if is_plat("linux") then
    option("wayland") -- use wayland (Linux only)
        set_default(false)
        set_showmenu(true)
        set_description("Enable Wayland support")
    option_end()
end

-- if build on windows
if is_plat("windows") then
    add_cxxflags("/Zc:__cplusplus", {tools = {"msvc", "cl"}}) -- fix __cplusplus == 199711L error
    add_cxxflags("/bigobj") -- avoid big obj
    add_cxxflags("-D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
    add_cxxflags("/EHsc")
    if is_mode("debug") then
        set_runtimes("MDd")
        add_links("ucrtd")
    else
        set_runtimes("MD")
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

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode", lsp = "clangd"})
add_rules("clangd.config", "linux.sdl.driver", "imguiconfig")

-- add repositories
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")

-- include external libraries
includes("external")

-- bulitin tasks and targets
includes("builtin")

-- include source
includes("source")

-- include tests
if is_config("tests") then
    includes("tests")
end

-- if build examples, then include examples
if is_config("examples") then
    includes("examples")
end