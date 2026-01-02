add_requires("imgui v1.92.0-docking", {configs = { vulkan = true, sdl3 = true, wchar32 = true}})
add_requires("volk", "zlib")

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
    add_packages("imgui", {public = true})
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

target("debug_draw")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")
    add_headerfiles("debug_draw/debug_draw.hpp")
    add_includedirs("debug_draw", {public = true}) -- public: let other targets to auto include
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

target("miniply")
    set_kind("static")
    add_headerfiles("miniply/**.h")
    add_includedirs("miniply", {public = true}) -- public: let other targets to auto include
    add_files("miniply/**.cpp")

target("spz")
    set_kind("static")
    add_headerfiles("spz/**.h")
    add_includedirs("spz/src/cc", {public = true}) -- public: let other targets to auto include
    add_files("spz/**.cc")
    add_packages("zlib")

target("vrdx")
    set_kind("static")
    add_headerfiles("vrdx/**.h")
    add_includedirs("vrdx/include", {public = true}) -- public: let other targets to auto include
    add_includedirs("vrdx/src/generated/", {public = true}) -- public: let other targets to auto include
    add_files("vrdx/**.cc")
    add_packages("volk")

includes("vasset")