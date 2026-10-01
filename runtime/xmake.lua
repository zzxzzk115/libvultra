target("vultra-runtime")
    set_kind("binary")
    set_default(false)
    if is_plat("linux") then
        add_ldflags("-Wl,--as-needed", "-static-libgcc", "-static-libstdc++")
    end
    add_deps("vultra-vgui", "vultra-scripting", "vultra-pack")
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

    before_build(function (target)
        local output = path.join(os.projectdir(), "build", ".tmp", "runtime-builtin.vpk")
        local candidate = output .. ".new"
        os.mkdir(path.directory(output))
        os.tryrm(candidate)
        import("core.project.project")
        local pack = project.target("vultra-pack")
        os.execv(pack:targetfile(), {"--builtins", os.projectdir(), candidate})
        local changed = not os.isfile(output) or io.readfile(output) ~= io.readfile(candidate)
        if changed then
            os.mv(candidate, output)
            local embed = path.join(os.scriptdir(), "src", is_plat("windows") and "builtin_pack.rc" or "builtin_pack.S")
            os.touch(embed)
        else
            os.tryrm(candidate)
        end
    end)
target_end()
