local is_root = (os.projectdir() == os.scriptdir())
if is_root then
    set_project("libvultra")
    set_version("0.1.0")
end
set_languages("cxx23")

if is_root then
    -- Running an example first checks its build and shared asset preparation dependencies.
    set_policy("run.autobuild", true)
end

-- global options
option("libvultra_window_backend")
    set_default("glfw")
    set_values("glfw", "sdl3")
    set_showmenu(true)
    set_description("Desktop window and input backend")
option_end()

option("libvultra_with_openxr")
    set_default(true)
    set_showmenu(true)
    set_description("Build the OpenXR module and example")
option_end()

option("libvultra_build_examples") -- build examples?
    set_default(is_root)
    set_showmenu(true)
    set_description("Enable vultra examples")
option_end()

option("libvultra_build_tests") -- build tests?
    set_default(is_root)
    set_showmenu(true)
    set_description("Enable vultra tests")
option_end()

-- if build on windows
if is_plat("windows") then
    add_cxxflags("/Zc:__cplusplus", {tools = {"msvc", "cl"}}) -- fix __cplusplus == 199711L error
    add_cxxflags("/bigobj") -- avoid big obj
    add_cxxflags("-D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
    add_cxxflags("/EHsc")

    -- Targets and packages must use the same MSVC runtime to avoid LNK2038.
    local msvc_runtime = is_mode("debug") and "MTd" or "MT"
    set_runtimes(msvc_runtime)

    -- set_runtimes() covers project targets; packages need the matching requirement.
    add_requireconfs("**", {configs = {runtimes = msvc_runtime}})
else
    add_cxxflags("-fexceptions")
end

-- Standalone editor tooling must not overwrite the parent project configuration.
if is_root then
    rule("clangd.config")
        on_config(function (target)
            if is_host("windows") then
                os.cp(".clangd.win", ".clangd")
            else
                os.cp(".clangd.nowin", ".clangd")
            end
        end)
    rule_end()

    rule("vultra.linux.delivery")
        on_config(function (target)
            if not target:is_plat("linux") then
                return
            end
            local links = {}
            for _, name in ipairs({"links", "syslinks"}) do
                for _, values in ipairs(target:get_from(name, "*") or {}) do
                    table.join2(links, table.wrap(values))
                end
            end
            -- A trailing ldflag cannot remove unused desktop libraries. Apply it around the inherited links.
            target:add("linkgroups", table.unique(links), {name = "vultra_delivery", group = true, as_needed = true})
        end)
    rule_end()

    add_rules("mode.debug", "mode.release")
    add_rules("plugin.vsxmake.autoupdate")
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode", lsp = "clangd"})
    add_rules("clangd.config")
end

-- add repositories
add_repositories("vultra-packages " .. path.join(os.scriptdir(), "external", "packages"))
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")

-- include external libraries
includes("external")

-- include source
includes("source")
if is_root then
    includes("tools")
    includes("runtime")
    includes("editor")
end

-- include tests
if has_config("libvultra_build_tests") then
    includes("tests")
end

-- if build examples, then include examples
if has_config("libvultra_build_examples") then
    includes("examples")
end

-- Checked-in bindings keep normal builds independent of Python and libclang.
if is_root then
    task("codegen")
        set_menu {
            usage = "xmake codegen [options]",
            options = {{"c", "check", "k", nil, "Check generated files without modifying them"}}
        }
        on_run(function ()
            import("core.base.option")
            local args = {path.join(os.projectdir(), "scripts", "codegen.py")}
            if option.get("check") then
                table.insert(args, "--check")
            end
            os.execv("python3", args)
        end)
    task_end()
end
