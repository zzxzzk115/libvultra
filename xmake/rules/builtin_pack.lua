-- vultra.builtin_pack: makes a target a self-contained binary that embeds builtin.vpk.
--
-- Adds to the target:
--   * a dependency on `builtinpack` (the host tool that writes builtin/generated/builtin.vpk),
--   * the platform embed file (Windows .rc / Linux+macOS .incbin .S),
--   * the per-binary mount accessor (builtin_pack_mount.cpp) that installs the pack as the
--     builtin:: resource source via vultra::mountBuiltinPack().
--
-- before_build regenerates builtin.vpk (mtime-guarded) BEFORE the target's embed file compiles,
-- so the embedded copy is always fresh. The thin export-template runtime does NOT add this rule;
-- it mounts a project VPK instead.
rule("vultra.builtin_pack")
    on_load(function (target)
        local embed = path.join(os.projectdir(), "builtin", "embed")

        target:add("deps", "builtinpack")

        if target:is_plat("windows") then
            target:add("files", path.join(embed, "builtin_pack.rc"))
        elseif target:is_plat("linux") or target:is_plat("macosx") then
            target:add("files", path.join(embed, "builtin_pack_unix.S"))
        end

        target:add("files", path.join(embed, "builtin_pack_mount.cpp"))
    end)

    before_build(function (target)
        local projectdir = os.projectdir()
        local outvpk     = path.join(projectdir, "builtin", "generated", "builtin.vpk")

        -- (logicalPath, sourceFile) manifest. Logical paths are read by the engine under the
        -- builtin:// scheme. Shaders + render graphs + fonts + LTC textures; cursors/render-graph
        -- JSON readers still use their headers and are migrated later.
        local entries = {}
        local function add(logical_dir, files)
            for _, f in ipairs(files) do
                table.insert(entries, { logical_dir .. "/" .. path.filename(f), f })
            end
        end
        add("shaders", os.files(path.join(projectdir, "builtin/shader_lib/*.vshlib")))
        add("shaders", os.files(path.join(projectdir, "builtin/shader_lib/*.vshweblib")))
        add("render",  os.files(path.join(projectdir, "builtin/render/*.vrg.json")))
        add("fonts",   os.files(path.join(projectdir, "builtin/fonts/*.ttf")))
        add("fonts",   os.files(path.join(projectdir, "builtin/fonts/*.otf")))
        for _, name in ipairs({ "ltc_1.dds", "ltc_2.dds" }) do
            local f = path.join(projectdir, "builtin/textures", name)
            if os.isfile(f) then
                table.insert(entries, { "textures/" .. name, f })
            end
        end
        -- Builtin environment maps (default skybox etc.), addressed as builtin://textures/...
        for _, f in ipairs(os.files(path.join(projectdir, "builtin/textures/environment_maps/*.vtexture"))) do
            table.insert(entries, { "textures/environment_maps/" .. path.filename(f), f })
        end

        -- mtime guard: skip regeneration when the pack is newer than every source.
        local newest = 0
        for _, e in ipairs(entries) do
            local m = os.mtime(e[2])
            if m and m > newest then newest = m end
        end
        if os.isfile(outvpk) and os.mtime(outvpk) >= newest then
            return
        end

        os.mkdir(path.directory(outvpk))
        local args = { outvpk }
        for _, e in ipairs(entries) do
            table.insert(args, e[1])
            table.insert(args, e[2])
        end
        os.execv(target:dep("builtinpack"):targetfile(), args)

        -- NOTE: xmake compiles the embed file (.rc RCDATA / .S .incbin) based on that file's own
        -- mtime, not the embedded builtin.vpk payload. A clean or `xmake build -r <target>` always
        -- embeds the fresh pack; an *incremental* build after changing a builtin resource's content
        -- may keep a stale embed until the embed file or its object is rebuilt. Builtin engine
        -- resources are stable, so this is an accepted, documented limitation.
    end)
rule_end()
