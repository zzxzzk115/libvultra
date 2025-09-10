target("dds-ktx")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")
	add_headerfiles("dds-ktx/dds-ktx.h")
    add_includedirs("dds-ktx", {public = true}) -- public: let other targets to auto include
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

target("renderdoc")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")
    add_headerfiles("renderdoc/renderdoc_app.h")
    add_includedirs("renderdoc", {public = true}) -- public: let other targets to auto include
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

includes("KTX-Software")