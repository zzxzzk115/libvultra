-- Shared WASM/Emscripten packaging rules.
--
-- Defines two globally-available rules used by both the examples and the
-- standalone runtime/export-template targets:
--   * resources.vpk_pack -- import + pack a project's resources into a .vpk before build
--   * wasm.link          -- inject the baseline emscripten link flags (and, optionally,
--                           a project --preload-file when vpk.* / wasm.vpk_path are set)
--
-- Include this from the root xmake.lua *before* the targets that use it so the
-- rules are registered when those targets are processed.

local function _vpk_setting(target, key, legacy_key)
    local value = target:values(key)
    if value == nil and legacy_key ~= nil then
        value = target:values(legacy_key)
    end
    return value
end

local function _wasm_setting(target, key, legacy_key)
    local value = target:values(key)
    if value == nil and legacy_key ~= nil then
        value = target:values(legacy_key)
    end
    return value
end

local function _env_u32(name)
    local value = os.getenv(name)
    if value == nil or value == "" then
        return nil
    end
    local num = tonumber(value)
    if num == nil then
        return nil
    end
    num = math.floor(num)
    if num < 0 then
        return nil
    end
    return num
end

local function _apply_wasm_memory_policy(target)
    -- Browser-friendly defaults for desktop-class WebGPU runtimes.
    -- Override via env vars (MB):
    --   VULTRA_WASM_INITIAL_MEMORY_MB
    --   VULTRA_WASM_MAXIMUM_MEMORY_MB
    local initial_mb = _env_u32("VULTRA_WASM_INITIAL_MEMORY_MB") or 128
    local maximum_mb = _env_u32("VULTRA_WASM_MAXIMUM_MEMORY_MB") or 2048

    if maximum_mb < initial_mb then
        maximum_mb = initial_mb
    end

    -- wasm32 practical ceiling in browsers.
    if maximum_mb > 2048 then
        maximum_mb = 2048
    end

    target:add("ldflags", "-sALLOW_MEMORY_GROWTH=1", {force = true})
    target:add("ldflags", "-sINITIAL_MEMORY=" .. tostring(initial_mb * 1024 * 1024), {force = true})
    target:add("ldflags", "-sMAXIMUM_MEMORY=" .. tostring(maximum_mb * 1024 * 1024), {force = true})
    -- Emscripten 6 backs a growable heap with a resizable ArrayBuffer by default, and
    -- TextDecoder.decode() rejects views over those on browsers that predate resizable-buffer
    -- support -- every wasm->JS string then throws and the engine dies silently. Keep the
    -- classic realloc-on-grow heap for portability.
    target:add("ldflags", "-sGROWABLE_ARRAYBUFFERS=0", {force = true})
end

local function _resolve_vpk_paths(target)
    local project_dir = _vpk_setting(target, "vpk.project_dir", "wasm_vpk.project_dir")
                        or get_config("project_dir") or os.projectdir()
    local resources_dir = _vpk_setting(target, "vpk.resources_dir", "wasm_vpk.resources_dir")
                          or path.join(project_dir, "resources")
    local generated_dir = _vpk_setting(target, "vpk.generated_dir", "wasm_vpk.generated_dir")
                          or path.join(project_dir, "build", ".generated", "wasm_resources")
    local output_vpk = _vpk_setting(target, "vpk.output_vpk", "wasm_vpk.output_vpk")
                       or path.join(generated_dir, "resources.vpk")
    local mount_path = _vpk_setting(target, "vpk.mount_path", "wasm_vpk.mount_path") or "/resources.vpk"
    return project_dir, resources_dir, generated_dir, output_vpk, mount_path
end

local function _collect_vpk_pack_args(target)
    local args = {}

    local include_paths = _vpk_setting(target, "vpk.include_paths", "wasm_vpk.include_paths")
    if include_paths ~= nil then
        for _, include_path in ipairs(include_paths) do
            table.insert(args, "--include")
            table.insert(args, include_path)
        end
    end

    local extra_args = _vpk_setting(target, "vpk.pack_args", "wasm_vpk.pack_args")
    if extra_args ~= nil then
        for _, arg in ipairs(extra_args) do
            table.insert(args, arg)
        end
    end

    return args
end

rule("resources.vpk_pack")
    on_load(function (target)
        local project_dir, resources_dir, generated_dir, output_vpk = _resolve_vpk_paths(target)
        target:data_set("vpk.project_dir", project_dir)
        target:data_set("vpk.resources_dir", resources_dir)
        target:data_set("vpk.generated_dir", generated_dir)
        target:data_set("vpk.output_vpk", output_vpk)
    end)

    on_build(function (target)
        import("core.project.depend")

        local project_dir   = target:data("vpk.project_dir")
        local resources_dir = target:data("vpk.resources_dir")
        local generated_dir = target:data("vpk.generated_dir")
        local output_vpk    = target:data("vpk.output_vpk")
        local pack_args     = _collect_vpk_pack_args(target)

        local import_enabled = _vpk_setting(target, "vpk.enable_import", "wasm_vpk.enable_import")
        if import_enabled == nil then
            import_enabled = true
        end
        local pack_enabled = _vpk_setting(target, "vpk.enable_pack", "wasm_vpk.enable_pack")
        if pack_enabled == nil then
            pack_enabled = true
        end
        if not import_enabled and not pack_enabled then
            return
        end

        os.mkdir(generated_dir)

        -- One host-vultra invocation per mode (no double import): cook = import + pack, or a single
        -- import-only / pack-only when only one is enabled. The scripts locate the host `vultra`
        -- executable and run `vultra asset <verb>`.
        local stem = (import_enabled and pack_enabled) and "cook" or (import_enabled and "import" or "pack")
        local script = _vpk_setting(target, "vpk." .. stem .. "_script", "wasm_vpk." .. stem .. "_script")
            or path.join(project_dir, "scripts", stem .. (is_host("windows") and ".ps1" or ".sh"))
        local wants_outvpk = pack_enabled -- import-only takes no output vpk

        local function cook()
            local script_args = { project_dir, resources_dir }
            if wants_outvpk then
                table.insert(script_args, output_vpk)
            end
            if is_host("windows") then
                local ps = { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, "-NoBootstrap" }
                table.join2(ps, script_args)
                if wants_outvpk then table.join2(ps, pack_args) end
                os.execv("powershell.exe", ps)
            else
                local sh = table.join({ script }, script_args)
                table.insert(sh, "--no-bootstrap")
                if wants_outvpk then table.join2(sh, pack_args) end
                os.execv("sh", sh)
            end
        end

        -- Input guard: only re-import/pack when the raw resources, pack args, the cook scripts or the
        -- output target change. Without this the cook re-ran on every `xmake build`/`xmake run`.
        -- Exclude resources/imported/** -- that is the importer's OWN output (asset_registry.tsv +
        -- cooked assets); including it would make every cook invalidate the next one (mtime churn).
        local inputs = {}
        for _, f in ipairs(os.files(path.join(resources_dir, "**"))) do
            local rel = path.relative(f, resources_dir):gsub("\\", "/")
            if not (rel == "imported" or rel:startswith("imported/")) then
                table.insert(inputs, f)
            end
        end
        if os.isfile(script) then table.insert(inputs, script) end
        local values = { output_vpk, tostring(import_enabled), tostring(pack_enabled) }
        for _, a in ipairs(pack_args) do table.insert(values, a) end

        depend.on_changed(function ()
            cook()
            if not os.isfile(output_vpk) then
                raise("resources.vpk_pack: cook did not produce %s", output_vpk)
            end
        end, { files = inputs, values = values, dependfile = path.join(generated_dir, ".vpk_cook.d") })
    end)
rule_end()

rule("wasm.link")
    on_load(function (target)
        if not is_plat("wasm") then
            return
        end

        -- Always inject the baseline wasm link flags required by our runtime path.
        target:add("ldflags", "--use-port=emdawnwebgpu", "-sUSE_GLFW=3", "-sASYNCIFY", {force = true})
        _apply_wasm_memory_policy(target)

        -- Optional extra flags (append-only). This avoids accidentally dropping required defaults.
        local extra_ldflags = _wasm_setting(target, "wasm.extra_ldflags", nil)
        if extra_ldflags == nil then
            extra_ldflags = _wasm_setting(target, "wasm.ldflags", "wasm_vpk.ldflags")
        end
        if extra_ldflags == nil then
            extra_ldflags = _vpk_setting(target, "vpk.wasm_ldflags", "wasm_vpk.ldflags")
        end
        if extra_ldflags and #extra_ldflags > 0 then
            target:add("ldflags", table.unpack(extra_ldflags), {force = true})
        end

        local shell_file = _wasm_setting(target, "wasm.shell_file", "wasm.shell_template")
        if shell_file ~= nil and shell_file ~= "" then
            target:add("ldflags", "--shell-file=" .. shell_file, {force = true})
        end

        local vpk_path = _wasm_setting(target, "wasm.vpk_path", nil)
        if vpk_path == nil then
            vpk_path = _vpk_setting(target, "vpk.output_vpk", "wasm_vpk.output_vpk")
        end

        if vpk_path ~= nil then
            local mount_path = _wasm_setting(target, "wasm.vpk_mount", nil)
            if mount_path == nil then
                mount_path = _vpk_setting(target, "vpk.mount_path", "wasm_vpk.mount_path") or "/resources.vpk"
            end
            target:add("ldflags", "--preload-file=" .. vpk_path .. "@" .. mount_path, {force = true})
        end

        local function _add_preload(src, dst)
            if src == nil then
                return
            end
            local mount = dst
            if mount == nil or mount == "" then
                mount = "/" .. path.filename(src)
            end
            target:add("ldflags", "--preload-file=" .. src .. "@" .. mount, {force = true})
        end

        local preload_files = _wasm_setting(target, "wasm.preload_files", "wasm.preload")
        if preload_files ~= nil then
            for _, entry in ipairs(preload_files) do
                if type(entry) == "string" then
                    local at = entry:find("@", 1, true)
                    if at ~= nil then
                        _add_preload(entry:sub(1, at - 1), entry:sub(at + 1))
                    else
                        _add_preload(entry, nil)
                    end
                elseif type(entry) == "table" then
                    _add_preload(entry.src or entry[1], entry.dst or entry[2])
                end
            end
        end

        local imgui_ini_src = _wasm_setting(target, "wasm.imgui_ini", "wasm.imgui_ini_path")
        if imgui_ini_src ~= nil then
            local imgui_ini_mount = _wasm_setting(target, "wasm.imgui_ini_mount", nil) or "/imgui.ini"
            _add_preload(imgui_ini_src, imgui_ini_mount)
        end

        -- Engine builtin assets are normally read from the on-disk builtin/ folder at runtime,
        -- which doesn't exist on wasm. Bake the ones the runtime needs into MEMFS at the exact path
        -- AssetSystem expects (builtin/textures/...), mirroring the .rc/.S embedding on desktop. The
        -- default skybox is referenced by every default EnvironmentComponent.
        --
        -- emscripten forbids mixing --embed-file and --preload-file in one target, so: embed when
        -- the target has no preloads (keeps the export template a clean .wasm with no .data), and
        -- preload alongside the others otherwise (e.g. example-demo-app preloads its project vpk).
        local has_preload = (vpk_path ~= nil) or (preload_files ~= nil and #preload_files > 0) or
                            (imgui_ini_src ~= nil)
        local builtin_embeds = {
            "textures/environment_maps/citrus_orchard_puresky_1k.vtexture",
        }
        for _, rel in ipairs(builtin_embeds) do
            local src = path.join(os.projectdir(), "builtin", rel)
            if os.isfile(src) then
                local flag = has_preload and "--preload-file=" or "--embed-file="
                target:add("ldflags", flag .. src .. "@/builtin/" .. rel, {force = true})
            end
        end

        -- Bake the unified builtin resource pack into MEMFS at /builtin.vpk, where
        -- builtin_pack_mount.cpp's wasm path reads it at startup and installs it as the builtin::
        -- source. The vultra.builtin_pack rule (added to the same target) produces the file in its
        -- before_build, so it exists by link time; no os.isfile guard is needed (and must not be,
        -- since a clean build links after the pack is generated, not before this on_load runs).
        local builtin_pack = path.join(os.projectdir(), "builtin", "generated", "builtin.vpk")
        local pack_flag    = has_preload and "--preload-file=" or "--embed-file="
        target:add("ldflags", pack_flag .. builtin_pack .. "@/builtin.vpk", {force = true})
    end)
rule_end()
