-- vultra.builtin_pack: makes a target a self-contained binary that ships builtin.vpk.
--
-- Adds to the target:
--   * a dependency on `builtinpack` -- the HOST tool that BOTH packs and, in its own after_build,
--     writes builtin/generated/builtin.vpk (see tools/xmake.lua). Depending on it therefore builds
--     the packer AND produces the pack; this rule no longer cooks anything itself.
--   * build.fence: the desktop .rc/.S embed reads builtin.vpk at COMPILE time, but a plain dep only
--     orders the LINK step -- xmake would otherwise compile this target's files in parallel with
--     builtinpack's own build+cook. The fence forces builtinpack (including its vpk-cooking
--     after_build) to be FULLY built before any of this target's files compile, so a single `xmake`
--     never races a not-yet-cooked pack -- no manual `xmake build builtinpack` first.
--   * the platform embed file (Windows .rc / Linux+macOS .incbin .S) for desktop single-binary
--     embedding -- wasm bakes the pack into MEMFS via wasm.link, android reads it from the APK;
--   * the per-binary mount accessor (builtin_pack_mount.cpp) that installs the pack as the
--     builtin:: resource source via vultra::mountBuiltinPack().
rule("vultra.builtin_pack")
    on_load(function (target)
        local embed = path.join(os.projectdir(), "builtin", "embed")

        -- builtinpack is a host packer; only build it where host == target (desktop). On cross
        -- builds (wasm/android) the pack is produced by a prior host build and consumed as-is, so we
        -- do NOT depend on / build builtinpack under the cross toolchain (it would resolve the wrong
        -- toolchain/packages and fail).
        if not (target:is_plat("wasm") or target:is_plat("android")) then
            target:add("deps", "builtinpack")
            -- The vpk is embedded at COMPILE time (.rc RCDATA / .S .incbin), but a plain dep only
            -- guarantees ordering at LINK time. build.fence makes builtinpack fully build (including
            -- the after_build that cooks builtin.vpk) before this target builds at all, so one
            -- `xmake` always finds a freshly cooked pack instead of racing builtinpack's own compile.
            target:set("policy.build.fence", true)
        end

        if target:is_plat("windows") then
            target:add("files", path.join(embed, "builtin_pack.rc"))
        elseif target:is_plat("linux") or target:is_plat("macosx") then
            target:add("files", path.join(embed, "builtin_pack_unix.S"))
        end

        target:add("files", path.join(embed, "builtin_pack_mount.cpp"))
    end)

    before_build(function (target)
        -- Cross builds (wasm/android) consume a host-cooked pack rather than running the host packer
        -- under the cross toolchain. The host build that cross workflows already require produces it
        -- (builtinpack's after_build, see tools/xmake.lua), so just require it to exist here.
        if target:is_plat("wasm") or target:is_plat("android") then
            local outvpk = path.join(os.projectdir(), "builtin", "generated", "builtin.vpk")
            if not os.isfile(outvpk) then
                raise("builtin.vpk not found at %s.\n" ..
                      "Cross-compiling consumes a host-cooked builtin pack. Build a desktop target " ..
                      "first to produce it, e.g.: xmake f -p <host> -a <arch> -m release && xmake build vultra-app",
                      outvpk)
            end
        end
    end)
rule_end()
