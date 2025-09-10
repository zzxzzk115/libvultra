-- set project name
set_project("libvultra")

-- set project version
set_version("0.1.0")

-- set language version: C++ 23
set_languages("cxx23")

-- global options
option("examples") -- build examples?
    set_default(true)
option_end()

option("tests") -- build tests?
    set_default(true)
option_end()

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

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode", lsp = "clangd"})
add_rules("clangd.config")

-- add repositories
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")

-- include external libraries
includes("external")

-- include source
includes("source")

-- include tests
if has_config("tests") then
    includes("tests")
end

-- if build examples, then include examples
if has_config("examples") then
    includes("examples")
end

-- phony target for install external libraries
target("install-externals")
    set_kind("phony")
    -- on install, copy external prebuilt library to install directory
    on_install(function (target)
        if is_plat("windows") then
            print("Copying KTX-Software files to install directory...")
            os.cp(path.join(os.projectdir(), "external", "KTX-Software", os.host(), os.arch(), "lib", "*.lib"), path.join(target:installdir(), "lib"))
            os.cp(path.join(os.projectdir(), "external", "KTX-Software", os.host(), os.arch(), "bin", "*.dll"), path.join(target:installdir(), "bin"))
            os.cp(path.join(os.projectdir(), "external", "KTX-Software", os.host(), os.arch(), "include", "**"), path.join(target:installdir(), "include"))
        end
    end)