target("vultra-runtime")
    set_kind("binary")
    set_default(false)
    add_rules("vultra.linux.delivery")
    if is_plat("linux") then
        add_ldflags("-static-libgcc", "-static-libstdc++")
    end
    add_deps("vultra-vgui", "vultra-scripting", "vultra-builtin-pack")
    if vultraFeatures().api_ui then
        add_defines("VULTRA_RUNTIME_GUI")
    end
    add_files("src/main.cpp")
    if is_plat("windows") then
        add_files("../builtin/builtin_pack.rc")
    elseif is_plat("linux", "macosx") then
        add_files("../builtin/builtin_pack.S")
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
