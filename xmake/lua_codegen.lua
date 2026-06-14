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

    -- The IR pipeline (below) needs compile_commands.json for include paths/
    -- defines; the compile_commands.autoupdate rule writes it to .vscode after a
    -- build, so the first ever build has none -> skip and use the committed
    -- artifacts. (Components are now part of the IR pipeline -- the legacy
    -- gen_lua_bindings.py was retired.)
    local cc = path.join(projectdir, ".vscode", "compile_commands.json")
    if not os.isfile(cc) then
        cprint("${color.warning}[lua-codegen] no .vscode/compile_commands.json yet; "
               .. "skipping binding regen (build once to enable)")
    end

    -- IR-based binding pipeline (extract_bindings.py -> gen_lua.py). Two stages,
    -- each stamp-guarded: stage 1 reparses C++ (libclang) only when an annotated
    -- header, the manifest, or the extractor changes; stage 2 re-emits sol2
    -- glue + the LuaLS stub only when the IR or a backend changes. The IR and
    -- generated files are checked in, so this whole block is skippable.
    local manifest = path.join(projectdir, "tools", "bindings", "headers.json")
    if os.isfile(cc) and os.isfile(manifest) then
        local headers = {}
        local json = import("core.base.json")
        local data = try { function() return json.loadfile(manifest) end }
        if data and data.headers then
            for _, h in ipairs(data.headers) do
                table.insert(headers, path.join(projectdir, h))
            end
        end
        local ir        = path.join(projectdir, "tools", "bindings", "ir", "bindings.ir.json")
        local extractor = path.join(projectdir, "tools", "python", "extract_bindings.py")
        local genlua    = path.join(projectdir, "tools", "python", "gen_lua.py")
        local bindgen   = os.files(path.join(projectdir, "tools", "python", "bindgen", "**.py"))

        -- stage 1: annotated headers -> IR
        local s1_stamp  = path.join(projectdir, "build", ".bindings-extract.stamp")
        local s1_inputs = table.join(headers, bindgen, {extractor, manifest})
        if fresh(s1_stamp, s1_inputs) then
            cprint("${dim}[lua-codegen] binding IR up to date")
        else
            local ok = try { function() os.vrunv(py, {extractor}); return true end }
            if ok then
                io.writefile(s1_stamp, os.date("%Y-%m-%d %H:%M:%S"))
                cprint("${color.success}[lua-codegen] binding IR extracted")
            else
                cprint("${color.warning}[lua-codegen] extract_bindings failed; using checked-in IR")
            end
        end

        -- stage 2: IR -> sol2 .gen.cpp + LuaLS stub
        local s2_stamp  = path.join(projectdir, "build", ".bindings-lua.stamp")
        local s2_inputs = table.join(bindgen, {genlua, ir})
        if fresh(s2_stamp, s2_inputs) then
            cprint("${dim}[lua-codegen] generated Lua bindings up to date")
        else
            local ok = try { function() os.vrunv(py, {genlua}); return true end }
            if ok then
                io.writefile(s2_stamp, os.date("%Y-%m-%d %H:%M:%S"))
                cprint("${color.success}[lua-codegen] generated Lua bindings regenerated")
            else
                cprint("${color.warning}[lua-codegen] gen_lua failed; using checked-in .gen.cpp")
            end
        end
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
