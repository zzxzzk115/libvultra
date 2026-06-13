-- Runs the Lua-binding code generators inside the project-local .venv.
--
-- Best-effort by design: the generated files (script_components_binding.gen.cpp,
-- script_imgui_binding.gen.cpp, and the generated sections of
-- tools/lua-stubs/vultra.lua) are CHECKED IN, so a build never depends on the
-- Python toolchain being present. This step regenerates them when the
-- annotated component headers change. Anything missing (no python, offline
-- pip, no compile_commands.json, no dear_bindings metadata) downgrades to a
-- printed notice and the checked-in files are used as-is.
--
-- Invoked by the `lua-codegen` phony target's on_build and by `xmake codegen`.

function main()
    local projectdir = os.projectdir()

    import("python_venv", { rootdir = path.join(projectdir, "xmake") })
    local py = python_venv({ soft = true })
    if not py then
        return -- python_venv already explained why; checked-in files stand
    end

    -- Cheap stamp-based guard: skip the (~10s libclang) regen when a stamp is
    -- newer than every input. A stamp is used rather than the .gen.cpp mtime
    -- because the generators write-if-changed (keeping the .gen.cpp mtime
    -- stable for compile hygiene), so the output mtime can't double as a
    -- freshness marker. The stamp is touched after every run.
    local function newest_mtime(paths)
        local newest = 0
        for _, p in ipairs(paths) do
            if os.isfile(p) and os.mtime(p) > newest then
                newest = os.mtime(p)
            end
        end
        return newest
    end
    local function fresh(stamp, inputs)
        return os.isfile(stamp) and os.mtime(stamp) >= newest_mtime(inputs)
    end

    -- gen_lua_bindings needs compile_commands.json for include paths/defines.
    -- The compile_commands.autoupdate rule writes it to .vscode after a build,
    -- so the first ever build has none -> skip and use the committed .gen.cpp.
    local cc = path.join(projectdir, ".vscode", "compile_commands.json")
    if os.isfile(cc) then
        local script     = path.join(projectdir, "tools", "python", "gen_lua_bindings.py")
        local stamp      = path.join(projectdir, "build", ".lua-codegen.stamp")
        local components = os.files(path.join(projectdir, "source", "vultra", "include", "vultra",
                                              "function", "world", "components", "*.hpp"))
        local inputs     = table.join(components, {script})
        if fresh(stamp, inputs) then
            cprint("${dim}[lua-codegen] component bindings up to date")
        else
            local ok = try { function() os.vrunv(py, {script}); return true end }
            if ok then
                io.writefile(stamp, os.date("%Y-%m-%d %H:%M:%S"))
                cprint("${color.success}[lua-codegen] component bindings regenerated")
            else
                cprint("${color.warning}[lua-codegen] gen_lua_bindings failed; using checked-in .gen.cpp")
            end
        end
    else
        cprint("${color.warning}[lua-codegen] no .vscode/compile_commands.json yet; "
               .. "skipping component-binding regen (build once to enable)")
    end

    -- gen_imgui_lua needs dear_bindings metadata (build/.tmp/cimgui.json), a
    -- one-time manual step documented in the script. Only regenerate when it
    -- is already present; never fetch/clone during a build.
    local cimgui = path.join(projectdir, "build", ".tmp", "cimgui.json")
    if os.isfile(cimgui) then
        local script = path.join(projectdir, "tools", "python", "gen_imgui_lua.py")
        local ok = try { function() os.vrunv(py, {script}); return true end }
        if ok then
            cprint("${color.success}[lua-codegen] ImGui bindings regenerated")
        else
            cprint("${color.warning}[lua-codegen] gen_imgui_lua failed; using checked-in .gen.cpp")
        end
    end
end
