-- Build-time host tools.

-- lua-codegen: regenerates the Lua binding glue (component usertypes, ImGui
-- subset, LuaLS stub) from VLUA_*-annotated headers + dear_bindings metadata,
-- inside a project-local .venv provisioned from tools/python/requirements.txt.
-- A phony target so it slots into the build graph: targets that consume the
-- generated .gen.cpp add_deps("lua-codegen"), so it runs before they compile.
-- Best-effort -- the generated files are checked in, so this is a no-op when
-- nothing changed and a graceful skip when the Python toolchain is absent
-- (see xmake/lua_codegen.lua / xmake/python_venv.lua).
target("lua-codegen")
    set_kind("phony")
    set_default(false)
    -- never run codegen under cross toolchains (host-only python tooling); the
    -- committed generated files are consumed as-is on wasm/android.
    on_build(function (target)
        if target:is_plat("wasm") or target:is_plat("android") then
            return
        end
        import("lua_codegen", { rootdir = path.join(os.projectdir(), "xmake") })
        lua_codegen()
    end)

-- `xmake codegen`: run the generators on demand (same path as the build hook).
task("codegen")
    set_menu {
        usage = "xmake codegen",
        description = "Regenerate Lua bindings (component usertypes, ImGui, stub) into a .venv.",
        options = {}
    }
    on_run(function ()
        import("lua_codegen", { rootdir = path.join(os.projectdir(), "xmake") })
        lua_codegen()
    end)


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

        -- Cook builtin.vpk via the shared, mtime-guarded packer (runs this binary directly -- never
        -- a nested `xmake`). The same module is used by vultra.builtin_pack's before_build, so the
        -- manifest lives in exactly one place. See xmake/builtin_pack_cook.lua.
        import("builtin_pack_cook", { rootdir = path.join(os.projectdir(), "xmake") })
        builtin_pack_cook(target:targetfile())

        -- NOTE: xmake compiles the embed file (.rc RCDATA / .S .incbin) based on that file's own
        -- mtime, not the embedded payload. A clean or `xmake build -r <target>` always embeds the
        -- fresh pack; an incremental build after changing a builtin resource may keep a stale embed
        -- until the embed file/object is rebuilt. Builtin engine resources are stable, so this is an
        -- accepted, documented limitation.
    end)
