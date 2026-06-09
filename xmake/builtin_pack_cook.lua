-- Shared builtin.vpk cook.
--
-- Packs libvultra's builtin engine resources into builtin/generated/builtin.vpk by running the
-- already-built `builtinpack` host binary over a (logicalPath, sourceFile) manifest. It is invoked
-- from two places, BOTH by running the binary DIRECTLY (never a nested `xmake build`, which would
-- contend on the build lock and deadlock):
--   * the builtinpack target's after_build (so `xmake build builtinpack` alone yields a fresh pack);
--   * the vultra.builtin_pack rule's before_build (so a consumer always has a fresh pack before it
--     compiles its embed file).
-- The repack is mtime-guarded, so calling it when nothing changed is a cheap no-op.
--
-- `packbin` is the absolute path to the built builtinpack executable (target:targetfile()).
function main(packbin)
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

    -- mtime guard: skip the repack when the pack is newer than every source. This script is
    -- included so editing the manifest above forces a repack even when the packed sources are older.
    local newest = os.mtime(path.join(projectdir, "xmake/builtin_pack_cook.lua")) or 0
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
    -- os.runv (not os.execv): run the packer quietly -- captured output is surfaced only on failure.
    os.runv(packbin, args)
    cprint("${green}[PACK]${clear} builtin.vpk (%d entries)", #entries)
end
