-- workaround for only linking vulkan library rather than glslang, spirv-cross, etc.
-- https://github.com/xmake-io/xmake-repo/issues/3962#issuecomment-2096205856
rule("vulkansdk")
    on_config(function (target)
        if target:is_plat("android") then
            target:add("syslinks", "vulkan", { public = true })
            return
        end

        import("lib.detect.find_library")
        import("detect.sdks.find_vulkansdk")

        local vulkansdk = find_vulkansdk()
        if vulkansdk then
            target:add("runevs", "PATH", vulkansdk.bindir)

            -- We don't need to add the include directories for vulkan headers
            -- Instead, we rely on Vulkan-Headers
            -- target:add("includedirs", vulkansdk.includedirs)

            local suffix
            if target:is_plat("windows") then
                suffix = ".lib"
            elseif target:is_plat("macosx") then
                suffix = ".dylib"
            else
                suffix = ".so"
            end

            local utils = {}
            table.insert(utils, target:is_plat("windows") and "vulkan-1" or "vulkan")

            ----------------------------------------------------------------
            -- macOS specific: add rpath so that executable can locate
            -- libvulkan.dylib at runtime. Without this, the program may
            -- fail to load Vulkan loader outside the SDK shell.
            ----------------------------------------------------------------
            if target:is_plat("macosx") then
                target:add("rpathdirs", vulkansdk.linkdirs[1], { public = true })

                -- Force linker to embed LC_RPATH entry
                target:add("ldflags",
                    "-Wl,-rpath," .. vulkansdk.linkdirs[1],
                    { force = true, public = true })
            end
            ----------------------------------------------------------------

            for _, util in ipairs(utils) do
                if not find_library(util, vulkansdk.linkdirs) then
                    wprint(format("The library %s for %s is not found!", util, target:arch()))
                    return
                end

                -- add vulkan library
                lib_name = target:is_plat("windows") and util or "lib" .. util
                lib_path = path.join(vulkansdk.linkdirs[1], lib_name .. suffix)
                print("Linking Vulkan library: " .. lib_path .. " for target: " .. target:name())

                target:add("links", lib_path, { public = true })
            end
        end
    end)
rule_end()

-- options
option("tracy")
    set_default(false)
    set_showmenu(true)
    set_description("Enable tracy profiler")
option_end()

option("tracky")
    set_default(false)
    set_showmenu(true)
    set_description("Enable tracky profiler")
option_end()

-- add requirements
add_requires("fmt", { system = false })
add_requires("spdlog", "magic_enum", "entt", "cereal", "sol2", "argparse")
add_requires("joltphysics v5.5.0", {configs = {debug = is_mode("debug"), shared = false, object_layer_bits = "16"}})
add_requires("vulkan-headers 1.4.335+0")
if not is_plat("wasm") then
    add_requires("vulkan-memory-allocator-hpp")
end
if not is_plat("android") then
    add_requires("webgpu-sdk v0.1.2", {configs = {shared = false}})
end
if not is_plat("wasm") then
    add_requireconfs("vulkan-memory-allocator-hpp", {configs = {use_vulkanheaders = true}})
end
add_requireconfs("**.vulkan-headers", {override = true, version = "1.4.309+0"}) -- unfortunately, some dependencies (e.g. vulkan-memory-allocator-hpp) still rely on older Vulkan-Headers, we need to override it to avoid version conflicts
if is_plat("windows") then
    add_requireconfs("**.libsdl3", {configs = {shared = false}})
    add_requireconfs("**.openxr", {configs = {shared = false}})
    add_requireconfs("**.webgpu-sdk", {configs = {shared = false}})
end
if has_config("tracy") then
    add_requires("tracy v0.12.2", {configs = {on_demand = true}})
end
if not is_plat("android") and not is_plat("wasm") then
    add_requireconfs("imgui.libsdl3", {system = false}) -- we don't use system's SDL3 to avoid version conflicts
end
if not is_plat("wasm") then
    add_requires("openxr", {configs = {shared = false, debug = is_mode("debug")}})
end
add_requires("vrendergraph v0.3.0", {configs = { debug = is_mode("debug") }})

-- target defination, name: vultra
target("vultra")
    -- set target kind: static library
    set_kind("static")
    if is_plat("android") then
        add_cflags("-fPIC")
        add_cxflags("-fPIC")
    end

    -- add include dir
    add_includedirs("vultra/include", {public = true}) -- public: let other targets to auto include
    if is_plat("android") then
        local game_activity_root = path.join(os.projectdir(), "external", "android", "game-activity_static")
        add_includedirs(path.join(game_activity_root, "include"), {public = true})
        add_syslinks("android", "log", {public = true})

        local arch = get_config("arch")
        local game_activity_lib = path.join(game_activity_root,
                                            "libs",
                                            "android." .. (arch or "arm64-v8a"),
                                            "libgame-activity_static.a")
        add_links(game_activity_lib, {public = true})
    end

    -- add header files
    add_headerfiles("vultra/include/(vultra/**.hpp)")

    -- add source files
    add_files("vultra/src/**.cpp")
    if is_plat("android") then
        remove_files("vultra/src/platform/sdl/**.cpp")
        remove_files("vultra/src/platform/glfw/**.cpp")
    elseif is_plat("wasm") then
        remove_files("vultra/src/platform/sdl/**.cpp")
        remove_files("vultra/src/platform/android/**.cpp")
    else
        remove_files("vultra/src/platform/android/**.cpp")
    end
    if is_plat("wasm") then
        remove_files("vultra/src/function/openxr/**.cpp")
        remove_files("vultra/src/core/rhi/backends/vk/**.cpp")
        remove_files("vultra/src/core/profiling/tracky.cpp")
        remove_files("vultra/src/core/profiling/renderdoc_api.cpp")
        remove_files("vultra/src/function/debugging/frame_debugger_system.cpp")
    end

    -- add deps
    add_deps("vasset", "renderdoc", "IconFontCppHeaders", "imgui-ext", "debug_draw", "vultra_builtin_assets")
    if not is_plat("wasm") then
        add_deps("vrdx")
    end
    if not is_plat("android") and not is_plat("wasm") then
        add_deps("vasset-import")
        add_defines("VULTRA_HAS_VASSET_IMPORT", { public = true })
    end

    -- add rules
    if not is_plat("wasm") then
        add_rules("vulkansdk")
    end

    -- add packages
    add_packages("fmt", "spdlog", "cereal", "magic_enum", "entt", "vrendergraph", "sol2", "joltphysics", { public = true })
    if not is_plat("wasm") then
        add_packages("vulkan-headers", "vulkan-memory-allocator-hpp", { public = true })
    else
        add_packages("vulkan-headers", { public = true })
    end
    if not is_plat("android") then
        add_packages("webgpu-sdk", { public = true })
    end
    if is_plat("wasm") then
        local emsdk = os.getenv("EMSDK")
        if emsdk then
            local emdawnwebgpu_include = path.join(emsdk,
                "upstream",
                "emscripten",
                "cache",
                "ports",
                "emdawnwebgpu",
                "emdawnwebgpu_pkg",
                "webgpu",
                "include")
            if os.isdir(emdawnwebgpu_include) then
                add_includedirs(emdawnwebgpu_include, {public = true})
            end
        end
    end
    if not is_plat("wasm") then
        add_packages("openxr", { public = true })
    end
    if not is_plat("android") then
        add_packages("libsdl3", { public = true })
    end
    if has_config("tracy") then
        add_packages("tracy", { public = true })
    end

    -- vulkan dynamic loader
    if not is_plat("wasm") then
        add_defines("VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1", { public = true })
        if is_plat("android") then
            add_defines("VULKAN_HPP_NO_SPACESHIP_OPERATOR=1", { public = true })
        end
    elseif is_plat("wasm") then
        add_defines("VULKAN_HPP_NO_SPACESHIP_OPERATOR=1", { public = true })
    end

    -- tracy & tracky required defines
    if has_config("tracy") then
        add_defines("TRACY_ENABLE=1", { public = true })
    end
    if has_config("tracky") and not is_plat("wasm") then
        add_defines("TRACKY_ENABLE=1", { public = true })
    end
    if not is_plat("wasm") then
        add_defines("TRACKY_VULKAN", { public = true })
    end

    -- fmt fix
    add_defines("FMT_UNICODE=0", { public = true })
    -- lock GLM clip/depth convention explicitly for this target
    add_defines("GLM_FORCE_DEPTH_ZERO_TO_ONE", "GLM_FORCE_RADIANS", { public = true })

    if is_mode("debug") then
        add_defines("_DEBUG", { public = true })
    else
        add_defines("NDEBUG", { public = true })
    end
    if is_plat("android") then
        add_defines("VULTRA_ENABLE_VULKAN=1", { public = true })
        add_defines("VULTRA_ENABLE_WEBGPU=0", { public = true })
        add_defines("VULTRA_ENABLE_XR=1", { public = true })
    elseif is_plat("wasm") then
        add_defines("VULTRA_ENABLE_VULKAN=0", { public = true })
        add_defines("VULTRA_ENABLE_WEBGPU=1", { public = true })
        add_defines("VULTRA_ENABLE_XR=0", { public = true })
    else
        add_defines("VULTRA_ENABLE_VULKAN=1", { public = true })
        add_defines("VULTRA_ENABLE_WEBGPU=1", { public = true })
        add_defines("VULTRA_ENABLE_XR=1", { public = true })
    end

    if is_plat("android") and get_config("android_allow_32bit_unsafe") then
        add_defines("VULTRA_ALLOW_UNSAFE_32BIT_VULKAN_HANDLES=1", { public = true })
    end

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vultra")

if not is_plat("android") and not is_plat("wasm") then
    target("vultra-app")
        set_kind("binary")
        set_basename("vultra")
        add_includedirs("vultra_app/include")
        add_headerfiles("vultra_app/include/(**.hpp)")
        add_files("vultra_app/src/**.cpp")
        if is_plat("windows") then
            add_files("vultra_app/resources/**.rc")
        elseif is_plat("linux") or is_plat("macosx") then
            add_files("vultra_app/resources/**.S")
        end
        add_deps("vultra", "vasset-import")
        add_packages("argparse")
        if has_config("vultra_app_validation") then
            add_defines("VULTRA_APP_DEFAULT_VALIDATION=1")
        else
            add_defines("VULTRA_APP_DEFAULT_VALIDATION=0")
        end
        if has_config("vultra_app_debug_markers") then
            add_defines("VULTRA_APP_DEFAULT_DEBUG_MARKERS=1")
        else
            add_defines("VULTRA_APP_DEFAULT_DEBUG_MARKERS=0")
        end
        if has_config("vultra_app_renderdoc") then
            add_defines("VULTRA_APP_DEFAULT_RENDERDOC=1")
        else
            add_defines("VULTRA_APP_DEFAULT_RENDERDOC=0")
        end
        set_rundir("$(projectdir)")
        set_runargs("--editor", "--project", "$(projectdir)/example.vproject")
        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vultra-app")
        after_build(function (target)
            local stale_imgui_ini = path.join(target:targetdir(), "imgui.ini")
            if os.isfile(stale_imgui_ini) then
                os.rm(stale_imgui_ini)
            end
        end)
end
