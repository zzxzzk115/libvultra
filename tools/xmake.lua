-- Build-time host tools.

-- builtinpack: packs libvultra's builtin engine resources into a single zstd VPK,
-- which vultra_builtin_pack embeds into self-contained binaries (editor, examples).
target("builtinpack")
    set_kind("binary")
    set_default(false) -- built on demand / as a dependency, not part of `xmake` default build
    add_files("builtinpack/main.cpp")
    add_deps("vasset")
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/builtinpack")
    -- The pack itself (builtin.vpk) is produced by the vultra.builtin_pack rule's before_build,
    -- which runs (mtime-guarded) before a self-contained binary embeds it.
