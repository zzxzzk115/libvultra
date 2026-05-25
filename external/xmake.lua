if is_plat("android") then
    add_requires("imgui v1.92.5-docking", {configs = { vulkan = true, android = true, wchar32 = true}})
elseif is_plat("wasm") then
    add_requires("imgui v1.92.5-docking", {configs = { wgpu = true, wgpu_backend = "webgpu-sdk", wchar32 = true}})
else
    add_requires("imgui v1.92.5-docking",
                 {configs = { vulkan = true, sdl3 = true, glfw = true, wgpu = true, wgpu_backend = "webgpu-sdk", wchar32 = true}})
end

add_requires("zlib")

includes("graphviz")

target("renderdoc")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")
    add_headerfiles("renderdoc/renderdoc_app.h")
    add_includedirs("renderdoc", {public = true}) -- public: let other targets to auto include
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

target("IconFontCppHeaders")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")
    add_headerfiles("IconFontCppHeaders/**.h")
    add_includedirs("IconFontCppHeaders", {public = true}) -- public: let other targets to auto include
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

target("imgui-ext")
    set_kind("static")
    add_headerfiles("imgui-ext/**.h", "imgui-ext/**.hpp")
    add_files("imgui-ext/**.cpp")
    add_includedirs("imgui-ext", {public = true}) -- public: let other targets to auto include
    add_defines("USE_STD_FILESYSTEM", {public = true})
    add_packages("imgui", {public = true})
    add_deps("graphviz")
    if is_plat("windows") then
        add_cxflags("/utf-8")
    end
    if is_plat("android") then
        add_cflags("-fPIC")
        add_cxflags("-fPIC")
    end
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

target("debug_draw")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")
    add_headerfiles("debug_draw/debug_draw.hpp")
    add_includedirs("debug_draw", {public = true}) -- public: let other targets to auto include
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

if not is_plat("wasm") then
    target("vrdx")
        set_kind("static")
        add_headerfiles("vrdx/**.h")
        add_includedirs("vrdx/include", {public = true}) -- public: let other targets to auto include
        add_includedirs("vrdx/src/generated/", {public = true}) -- public: let other targets to auto include
        add_files("vrdx/**.cc")
        add_packages("vulkan-headers", {public = true})
end

includes("vasset")
