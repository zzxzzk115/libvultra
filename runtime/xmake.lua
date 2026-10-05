target("vultra-builtin-pack")
    set_kind("phony")
    set_default(false)
    set_policy("build.fence", true)
    add_deps("vultra-pack")
    on_build(function (target)
        local output = path.join(os.projectdir(), "build", ".tmp", "runtime-builtin.vpk")
        local candidate = output .. ".new"
        os.mkdir(path.directory(output))
        os.tryrm(candidate)
        local pack = target:dep("vultra-pack")
        os.execv(pack:targetfile(), {"--builtins", os.projectdir(), candidate})
        local changed = not os.isfile(output) or io.readfile(output) ~= io.readfile(candidate)
        if changed then
            os.mv(candidate, output)
            local embed = path.join(os.projectdir(), "runtime", "src", is_plat("windows") and "builtin_pack.rc" or "builtin_pack.S")
            -- Recompile the .S/.rc source only when the embedded archive changes.
            io.writefile(embed, io.readfile(embed))
        else
            os.tryrm(candidate)
        end
    end)
target_end()

target("vultra-runtime")
    set_kind("binary")
    set_default(false)
    add_rules("vultra.linux.delivery")
    if is_plat("linux") then
        add_ldflags("-static-libgcc", "-static-libstdc++")
    end
    add_deps("vultra-vgui", "vultra-scripting", "vultra-builtin-pack")
    add_files("src/main.cpp")
    if is_plat("windows") then
        add_files("src/builtin_pack.rc")
    elseif is_plat("linux", "macosx") then
        add_files("src/builtin_pack.S")
    end
    set_rundir("$(projectdir)")

    if is_plat("linux") then
        after_build(function (target)
            local dynamic = os.iorunv("readelf", {"-d", target:targetfile()})
            for _, library in ipairs({"libslang", "libRmlUi", "librmlui", "libfreetype", "libz.so", "libopenxr_loader", "libdeflate", "liblua", "libstdc++", "libgcc_s"}) do
                if dynamic:find("Shared library: [" .. library, 1, true) then
                    raise("vultra-runtime still requires a shared engine dependency: %s", library)
                end
            end
            if dynamic:find("(RUNPATH)", 1, true) or dynamic:find("(RPATH)", 1, true) then
                raise("vultra-runtime must not depend on build-machine library search paths")
            end
        end)
    end
target_end()
