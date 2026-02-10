-- workaround for only linking vulkan library rather than glslang, spirv-cross, etc.
-- https://github.com/xmake-io/xmake-repo/issues/3962#issuecomment-2096205856
rule("vulkansdk")
    on_config(function (target)
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
            -- Add Vulkan library search directories
            ----------------------------------------------------------------
            target:add("linkdirs", vulkansdk.linkdirs, { public = true })

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

                -- Add vulkan library by name instead of absolute path
                -- This allows rpath and dynamic loading to work correctly
                print("Linking Vulkan library: " .. util .. " for target: " .. target:name())

                target:add("links", util, { public = true })
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

if not is_plat("linux") and not has_config("wayland") then
    option("renderdoc")
        set_default(true)
        set_showmenu(true)
        set_description("Enable renderdoc support")
    option_end()
else
    set_config("renderdoc", false)
end

option("vk_validation_stack_trace")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Vulkan validation stack trace")
option_end()

-- add requirements
add_requires("fmt", { system = false })
add_requires("spdlog", "magic_enum", "entt", "cereal", "vulkan-headers 1.4.309+0", "vulkan-memory-allocator-hpp", "fg", "cpptrace")
if has_config("tracy") then
    add_requires("tracy v0.12.2", {configs = {on_demand = true}})
end
add_requireconfs("imgui.libsdl3", {system = false}) -- we don't use system's SDL3 to avoid version conflicts
add_requires("openxr", {configs = {shared = true, debug = is_mode("debug")}})
-- note: spirv-cross & glslang must require the same vulkan sdk version
add_requires("spirv-cross vulkan-sdk-1.4.309", {configs = { shared = true, debug = is_mode("debug")}, system = false})
add_requires("glslang 1.4.309+0", {configs = {debug = is_mode("debug")}, system = false})

-- target defination, name: vultra
target("vultra")
    -- set target kind: static library
    set_kind("static")

    -- add include dir
    add_includedirs("include", {public = true}) -- public: let other targets to auto include

    -- add header files
    add_headerfiles("include/(vultra/**.hpp)")

    -- add source files
    add_files("src/**.cpp")

    -- add deps
    add_deps("vasset", "renderdoc", "IconFontCppHeaders", "imgui-ext", "debug_draw", "vultra_builtin_assets", "miniply", "spz")

    -- add rules
    add_rules("vulkansdk")

    -- add packages
    add_packages("fmt", "spdlog", "cereal", "magic_enum", "entt", "vulkan-headers", "vulkan-memory-allocator-hpp", "fg", "cpptrace", "tinyexr", { public = true })
    add_packages("libsdl3", "spirv-cross", "glslang", "openxr", { public = true })
    if has_config("tracy") then
        add_packages("tracy", { public = true })
    end

    -- vulkan dynamic loader
    add_defines("VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1", { public = true })

    -- tracy & tracky required defines
    if has_config("tracy") then
        add_defines("TRACY_ENABLE=1", { public = true })
    end
    if has_config("tracky") then
        add_defines("TRACKY_ENABLE=1", { public = true })
    end
    add_defines("TRACKY_VULKAN", { public = true })

    -- fmt fix
    add_defines("FMT_UNICODE=0", { public = true })

    if is_mode("debug") then
        add_defines("_DEBUG", { public = true })
        if has_config("renderdoc") then
            add_defines("VULTRA_ENABLE_RENDERDOC", { public = true })
        end
        if has_config("vk_validation_stack_trace") then
            add_defines("VULTRA_ENABLE_VK_VALIDATION_STACK_TRACE", { public = true })
        end
    else
        add_defines("NDEBUG", { public = true })
    end

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vultra")
