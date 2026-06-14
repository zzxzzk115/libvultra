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
add_requires("lz4") -- runtime decompression of lz4-block-compressed builtin blobs (embedded fonts)
add_requires("freetype") -- in-game UI text: rasterize UiTextComponent glyphs into the glyph atlas
local jolt_configs = {debug = is_mode("debug"), shared = false, object_layer_bits = "16"}
if is_plat("wasm") then
    -- physics_system.cpp subclasses JPH::JobSystemWithBarrier; under clang/Itanium the derived
    -- class typeinfo references the base typeinfo, which Jolt only emits when built with C++ RTTI.
    -- (Desktop/MSVC links without this.)
    jolt_configs.rtti = true
end
add_requires("joltphysics v5.5.0", {configs = jolt_configs})
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
add_requires("ozz-animation", {configs = {tools = false, fbx = false, gltf = false, data = false, debug = is_mode("debug")}})

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
        -- (vulkan_render_device.cpp's ray-tracing entry points are removed with the vk backend
        -- above; raytracing_stub_no_vulkan.cpp supplies inert ones. The Jolt RTTI typeinfo that
        -- physics_system.cpp needs is provided by enabling the joltphysics 'rtti' config on wasm —
        -- see add_requires below.)
    end

    -- add deps
    add_deps("vasset", "renderdoc", "IconFontCppHeaders", "imgui-ext", "debug_draw", "vultra_builtin_assets")
    -- regenerate Lua bindings from VLUA_*-annotated headers before compiling
    -- (phony, host-only, best-effort -- see tools/xmake.lua). The generated
    -- .gen.cpp files are checked in, so cross builds and toolchain-less builds
    -- simply consume them.
    if not is_plat("wasm") and not is_plat("android") then
        add_deps("lua-codegen")
    end
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
    add_packages("fmt", "spdlog", "cereal", "magic_enum", "entt", "vrendergraph", "sol2", "joltphysics", "ozz-animation", { public = true })
    add_packages("lz4") -- private: only imgui_system.cpp decompresses embedded fonts
    add_packages("freetype") -- private: only the glyph atlas (function/rendering/text) uses the FreeType C API
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
        -- The standalone runtime player has its own entry point (a second main()); it is built by
        -- the cross-platform vultra-runtime target below, not folded into the editor binary.
        remove_files("vultra_app/src/runtime/**.cpp")
        if is_plat("windows") then
            add_files("vultra_app/resources/**.rc")
        elseif is_plat("linux") or is_plat("macosx") then
            add_files("vultra_app/resources/**.S")
        end
        add_deps("vultra", "vasset-import")
        add_rules("vultra.builtin_pack")
        add_packages("argparse")
        if is_plat("windows") then
            add_syslinks("ws2_32", "winhttp")
            -- Merge a UTF-8 active-code-page manifest so std::filesystem / ImGuiFileDialog path
            -- conversions (WideCharToMultiByte(CP_ACP)) never fail on non-ASCII names under a
            -- non-UTF-8 system code page (e.g. GBK). See vultra_app/resources/vultra.manifest.
            add_ldflags("/manifest:embed",
                        "/manifestinput:" .. path.join(os.scriptdir(), "vultra_app/resources/vultra.manifest"),
                        {force = true})
        end
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
        -- Surface the engine version to the editor status bar. Read straight from set_version() in
        -- the root xmake.lua so that remains the single source of truth (no duplicated literal here).
        on_load(function (target)
            import("core.project.project")
            target:add("defines", 'VULTRA_ENGINE_VERSION="' .. (project.version() or "dev") .. '"')
        end)
        set_rundir("$(projectdir)")
        set_runargs("--editor", "--mcp", "--project", "$(projectdir)/example.vproject")
        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vultra-app")
        after_build(function (target)
            local stale_imgui_ini = path.join(target:targetdir(), "imgui.ini")
            if os.isfile(stale_imgui_ini) then
                os.rm(stale_imgui_ini)
            end
        end)
end

-- Standalone runtime player: the editor-free "player" that runs a packaged project (.vpk).
-- This is the single source for every platform's export template, so it builds for desktop,
-- wasm (Emscripten) and android. It intentionally pulls in only a small slice of vultra_app
-- (the runtime entry, project loading and launch-option parsing) and none of the editor.
target("vultra-runtime")
    if is_plat("android") then
        set_kind("shared")
        set_basename("vultra_runtime")
    else
        set_kind("binary")
        set_basename("vultra-runtime")
    end
    add_includedirs("vultra_app/include")
    add_files("vultra_app/src/runtime/runtime_main.cpp",
              "vultra_app/src/vproject.cpp",
              "vultra_app/src/launch_options.cpp")
    add_deps("vultra")
    -- The runtime is self-contained on every platform: it ships builtin.vpk and mounts it as the
    -- builtin:: source. The vultra.builtin_pack rule handles the platform-specific delivery --
    -- desktop embeds it in the binary (.rc/.S), wasm bakes it into MEMFS (wasm.link --embed-file),
    -- and android reads it from the APK in its runtime entry. See builtin/embed/builtin_pack_mount.cpp.
    add_rules("vultra.builtin_pack")
    add_packages("argparse")
    if is_plat("windows") then
        add_syslinks("ws2_32")
        -- UTF-8 active code page (see vultra-app above / vultra_app/resources/vultra.manifest).
        add_ldflags("/manifest:embed",
                    "/manifestinput:" .. path.join(os.scriptdir(), "vultra_app/resources/vultra.manifest"),
                    {force = true})
    elseif is_plat("android") then
        add_syslinks("android", "log")
        on_load(function (target)
            -- Mirror examples/android_app: link libgame-activity.a from the gradle prefab cache.
            local user_home = os.getenv("USERPROFILE") or os.getenv("HOME") or ""
            local gradle_home = os.getenv("GRADLE_USER_HOME") or path.join(user_home, ".gradle")
            local arch = target:arch()
            local game_activity_arch = "android." .. arch
            local libs = os.files(path.join(gradle_home,
                                            "caches",
                                            "**",
                                            "games-activity-*",
                                            "prefab",
                                            "modules",
                                            "game-activity",
                                            "libs",
                                            game_activity_arch,
                                            "libgame-activity.a"))
            if #libs > 0 then
                target:add("links", libs[1])
            end
        end)
    end
    if is_plat("wasm") then
        -- Engine-only web template: no project --preload-file (no vpk.* values set), so the
        -- runtime fetches the project's game.vpk at page load instead of baking it in.
        add_rules("wasm.link")
        set_values("wasm.shell_file", path.join(os.projectdir(), "web", "emscripten_vultra_runtime.html"))
        -- The shell's preRun fetches the VPK into MEMFS, so the FS and run-dependency runtime
        -- methods must be exported, and the filesystem must be forced in (no preloaded data).
        set_values("wasm.extra_ldflags",
                   {
                       "-sFORCE_FILESYSTEM=1",
                       "-sEXPORTED_RUNTIME_METHODS=['FS','callMain','addRunDependency','removeRunDependency']",
                   })
        -- Refresh the editor's default web export template from the freshly built engine bundle:
        -- index.html + the js/wasm. This is a BUILD ARTIFACT (the engine-only runtime), so it lives
        -- under build/ (git-ignored), not in a source dir. The editor copies it next to a packed
        -- game.vpk at export time; defaultWebTemplate() in editor_app_build.cpp points here.
        after_build(function (target)
            local out = path.join(os.projectdir(), "build", "web-template")
            os.mkdir(out)
            local dir = target:targetdir()
            os.cp(path.join(dir, "vultra-runtime.js"), out)
            os.cp(path.join(dir, "vultra-runtime.wasm"), out)
            os.cp(path.join(dir, "vultra-runtime.html"), path.join(out, "index.html"))
            print("Packaged web export template -> " .. out)
        end)
    end
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vultra-runtime")
