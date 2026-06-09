-- Build-time host tools.

-- builtinpack: packs libvultra's builtin engine resources into a single zstd VPK,
-- which vultra.builtin_pack embeds into self-contained binaries (editor, examples).
target("builtinpack")
    set_kind("binary")
    set_default(false) -- built on demand / as a dependency, not part of `xmake` default build
    add_files("builtinpack/main.cpp")
    add_deps("vasset")
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/builtinpack")

    -- builtinpack produces its OWN artifact: once the packer links, its after_build cooks
    -- builtin/generated/builtin.vpk from the builtin resource tree. Keeping the cook here (rather
    -- than in a consumer's before_build) gives a single source of truth -- `xmake build builtinpack`
    -- alone yields a fresh pack -- and lets consumers just `add_deps("builtinpack")` (+ build.fence,
    -- see xmake/rules/builtin_pack.lua) to have it ready before they compile their embed file.
    -- The cook is mtime-guarded, so it is a silent no-op on incremental builds / `xmake run` when no
    -- builtin resource changed (after_build still runs when the target is up to date).
    after_build(function (target)
        -- builtinpack is a HOST packer: on cross builds (wasm/android) it is neither runnable nor
        -- needed (those consume a host-cooked pack), so never try to run it there. In practice
        -- set_default(false) keeps it out of cross builds entirely; this is a belt-and-braces guard.
        if target:is_plat("wasm") or target:is_plat("android") then
            return
        end

        local projectdir = os.projectdir()
        local outvpk     = path.join(projectdir, "builtin", "generated", "builtin.vpk")

        -- (logicalPath, sourceFile) manifest. Logical paths are read by the engine under the
        -- builtin:// scheme. Everything that previously compiled in as a C-array header ships here.
        local entries = {}
        local function add(logical_dir, files)
            for _, f in ipairs(files) do
                table.insert(entries, { logical_dir .. "/" .. path.filename(f), f })
            end
        end
        -- Keyed by each file's path relative to `root`, under logical prefix `prefix`.
        local function addrel(prefix, root, files)
            for _, f in ipairs(files) do
                local rel = path.relative(f, root):gsub("\\", "/")
                table.insert(entries, { prefix .. "/" .. rel, f })
            end
        end

        add("shaders", os.files(path.join(projectdir, "builtin/shader_lib/*.vshlib")))
        add("shaders", os.files(path.join(projectdir, "builtin/shader_lib/*.vshweblib")))
        add("render",  os.files(path.join(projectdir, "builtin/render/*.vrg.json")))
        add("fonts",   os.files(path.join(projectdir, "builtin/fonts/*.ttf")))
        add("fonts",   os.files(path.join(projectdir, "builtin/fonts/*.otf")))
        add("i18n",    os.files(path.join(projectdir, "builtin/i18n/*.json")))

        -- Textures keyed by path relative to builtin/textures: LTC LUTs, environment maps,
        -- ImGui/camera cursors, and editor icons. Addressed as builtin://textures/...
        local texroot = path.join(projectdir, "builtin/textures")
        for _, name in ipairs({ "ltc_1.dds", "ltc_2.dds" }) do
            local f = path.join(texroot, name)
            if os.isfile(f) then table.insert(entries, { "textures/" .. name, f }) end
        end
        addrel("textures", texroot, os.files(path.join(texroot, "environment_maps/*.vtexture")))
        addrel("textures", texroot, os.files(path.join(texroot, "kenney_cursor-pack/PNG/Outline/Default/*.png")))
        addrel("textures", texroot, os.files(path.join(texroot, "editor/*.png")))

        -- Shader GLSL include sources for the editor's asset importer (virtual includes),
        -- under builtin://shaders/include/...
        addrel("shaders", path.join(projectdir, "builtin/shaders"),
               os.files(path.join(projectdir, "builtin/shaders/include/**.glsl")))

        -- mtime guard: skip the repack when the pack is newer than every source. This script
        -- (tools/xmake.lua) is included so editing the manifest above forces a repack even when the
        -- packed sources are older.
        local newest = os.mtime(path.join(projectdir, "tools/xmake.lua")) or 0
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
        -- os.runv (not os.execv): run the packer quietly -- no subprocess output on success, and
        -- the captured output is only surfaced if it fails.
        os.runv(target:targetfile(), args)
        cprint("${green}[PACK]${clear} builtin.vpk (%d entries)", #entries)

        -- NOTE: xmake compiles the embed file (.rc RCDATA / .S .incbin) based on that file's own
        -- mtime, not the embedded payload. A clean or `xmake build -r <target>` always embeds the
        -- fresh pack; an incremental build after changing a builtin resource may keep a stale embed
        -- until the embed file/object is rebuilt. Builtin engine resources are stable, so this is an
        -- accepted, documented limitation.
    end)
