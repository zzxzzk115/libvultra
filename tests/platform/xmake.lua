set_project("vultra-platform-probe")
set_languages("cxx23")
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "$(builddir)"})

option("platform_pack")
    set_description("Existing platform probe project.vpk produced by platform-pack on the host")
option_end()
includes("../..")
local features = vultraFeatures()

target("platform-vpk-memory")
    set_kind("binary")
    add_deps("vultra")
    add_files("../vpk_memory.cpp")
    if is_plat("wasm") then
        add_ldflags("-fexceptions", "-sALLOW_MEMORY_GROWTH=1", "-sEXIT_RUNTIME=1", {force = true})
    end
target_end()

if features.shader and not is_plat("wasm", "android") then
    target("platform-pack")
        set_kind("binary")
        add_deps("vultra")
        add_files("pack.cpp")
    target_end()
end

if get_config("platform_pack") then
    if features.shader_runtime and not is_plat("android") then
        target("platform-shader-runtime")
            set_kind("binary")
            add_deps("vultra")
            add_files("shader_runtime.cpp", "../../examples/common/colored_mesh.cpp")
            add_rules("utils.bin2c", {extensions = ".vpk", nozeroend = true})
            add_files(get_config("platform_pack"))
            if is_plat("wasm") then
                set_extension(".html")
                add_ldflags("-sASYNCIFY", "-sALLOW_MEMORY_GROWTH=1", "-sGROWABLE_ARRAYBUFFERS=0",
                            "-sEXIT_RUNTIME=1", {force = true})
            end
        target_end()
    end
    target("platform-render")
        set_kind("binary")
        add_deps("vultra")
        add_files("render.cpp", "multipass.cpp", "compute.cpp")
        if is_plat("android") then
            set_kind("shared")
            add_files("android_main.cpp")
            add_syslinks("log")
            on_load(function (target)
                local ndk = target:toolchain("ndk"):config("ndk")
                local glue = path.join(ndk, "sources/android/native_app_glue")
                target:add("includedirs", glue)
                target:add("files", path.join(glue, "android_native_app_glue.c"))
            end)
        else
            add_rules("utils.bin2c", {extensions = ".vpk", nozeroend = true})
            add_files(get_config("platform_pack"))
        end
        if is_plat("wasm") then
            set_extension(".html")
            add_ldflags("-fexceptions", "-sASYNCIFY", "-sALLOW_MEMORY_GROWTH=1", "-sEXIT_RUNTIME=1",
                        "-sGROWABLE_ARRAYBUFFERS=0", {force = true})
        end
    target_end()
end
