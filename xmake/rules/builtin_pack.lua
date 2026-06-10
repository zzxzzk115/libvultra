-- vultra.builtin_pack: makes a target a self-contained binary that ships builtin.vpk.
--
-- The desktop embed (.rc RCDATA / .S .incbin) reads builtin/generated/builtin.vpk at COMPILE time,
-- so the pack must exist before this target compiles its files. The single, reliable ordering point
-- is this rule's before_build: xmake always runs a target's before_build before it compiles any of
-- that target's own files. before_build runs the already-built `builtinpack` binary DIRECTLY (via the
-- shared, mtime-guarded xmake/builtin_pack_cook.lua) to cook the pack -- never a nested `xmake build`
-- (that would deadlock on the build lock), and no reliance on cross-target fence/parallelism timing.
--
-- Adds to the target:
--   * a dependency on `builtinpack` (build inclusion + link ordering);
--   * the platform embed file (Windows .rc / Linux+macOS .incbin .S) for desktop single-binary
--     embedding -- wasm bakes the pack into MEMFS via wasm.link, android reads it from the APK;
--   * the per-binary mount accessor (builtin_pack_mount.cpp) that installs the pack as the
--     builtin:: resource source via vultra::mountBuiltinPack().
rule("vultra.builtin_pack")
    on_load(function (target)
        local embed = path.join(os.projectdir(), "builtin", "embed")

        -- builtinpack is a host packer; only depend on it where host == target (desktop). On cross
        -- builds (wasm/android) the pack is produced by a prior host build and consumed as-is, so we
        -- do NOT depend on / build builtinpack under the cross toolchain (it would resolve the wrong
        -- toolchain/packages and fail). Ordering for desktop is handled in before_build below.
        if not (target:is_plat("wasm") or target:is_plat("android")) then
            target:add("deps", "builtinpack")
        end

        if target:is_plat("windows") then
            target:add("files", path.join(embed, "builtin_pack.rc"))
        elseif target:is_plat("linux") or target:is_plat("macosx") then
            target:add("files", path.join(embed, "builtin_pack_unix.S"))
        end

        target:add("files", path.join(embed, "builtin_pack_mount.cpp"))
    end)

    before_build(function (target)
        local outvpk = path.join(os.projectdir(), "builtin", "generated", "builtin.vpk")

        -- Cross builds (wasm/android) consume a host-cooked pack rather than running the host packer
        -- under the cross toolchain. The host build that cross workflows already require produces it
        -- (builtinpack's after_build, see tools/xmake.lua), so just require it to exist here.
        if target:is_plat("wasm") or target:is_plat("android") then
            if not os.isfile(outvpk) then
                raise("builtin.vpk not found at %s.\n" ..
                      "Cross-compiling consumes a host-cooked builtin pack. Build a desktop target " ..
                      "first to produce it, e.g.: xmake f -p <host> -a <arch> -m release && xmake build vultra-app",
                      outvpk)
            end
            return
        end

        -- Desktop: ensure a fresh pack before we compile the embed file. add_deps("builtinpack")
        -- builds the host packer before this target's build, so its binary exists here; run it
        -- DIRECTLY via the shared, mtime-guarded cook (never a nested `xmake build`, which would
        -- contend on the build lock and deadlock). Cheap no-op when nothing changed, repack when a
        -- builtin resource did -- covering both the clean-build and the stale-pack cases.
        import("core.project.project")
        local pack    = project.target("builtinpack")
        local packbin = pack and pack:targetfile()
        if packbin and os.isfile(packbin) then
            import("builtin_pack_cook", { rootdir = path.join(os.projectdir(), "xmake") })
            builtin_pack_cook(packbin)
        end
        if not os.isfile(outvpk) then
            raise("builtin.vpk was not produced at %s.\n" ..
                  "The builtinpack host tool must build first (it is an add_deps dependency). " ..
                  "Try `xmake build builtinpack` and check its output.", outvpk)
        end

        -- The embed file (.rc RCDATA / .S .incbin) bakes builtin.vpk into the binary at COMPILE
        -- time, but neither xmake nor the RC/assembler tracks that incbin dependency. So when only
        -- the pack content changes (e.g. a shader-only edit repacks the vpk but no .rc/.cpp source
        -- changed), the embed object is considered up to date, the binary relinks with the STALE
        -- embedded pack, and the runtime keeps loading old builtin resources/shaders. Force the embed
        -- source to be newer than the pack so it recompiles and re-embeds the fresh vpk.
        local embed     = path.join(os.projectdir(), "builtin", "embed")
        local embedfile = nil
        if target:is_plat("windows") then
            embedfile = path.join(embed, "builtin_pack.rc")
        elseif target:is_plat("linux") or target:is_plat("macosx") then
            embedfile = path.join(embed, "builtin_pack_unix.S")
        end
        if embedfile and os.isfile(embedfile) and os.mtime(outvpk) > os.mtime(embedfile) then
            os.touch(embedfile, {mtime = os.time()})
        end
    end)
rule_end()
