package("openxr-static")
    set_homepage("https://github.com/KhronosGroup/OpenXR-SDK")
    set_description("OpenXR loader with vendored JSON parser for static linking")
    set_license("Apache-2.0")
    add_urls("https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-$(version).tar.gz")
    add_versions("1.1.49", "74e9260a1876b0540171571a09bad853302ec68a911200321be8b0591ca94111")
    add_deps("cmake", "python 3.x", {kind = "binary"})
    add_deps("libx11")
    add_syslinks("pthread", "dl")

    on_install("linux|x86_64", function (package)
        local configs = {
            "-DBUILD_LOADER=ON",
            "-DBUILD_TESTS=OFF",
            "-DBUILD_API_LAYERS=OFF",
            "-DBUILD_WITH_SYSTEM_JSONCPP=OFF",
            "-DDYNAMIC_LOADER=OFF",
            "-DBUILD_SHARED_LIBS=OFF",
            "-DOPENXR_DEBUG_POSTFIX=''"
        }
        import("package.tools.cmake").install(package, configs, {packagedeps = "libx11", jobs = 2})
    end)

    on_test(function (package)
        assert(package:has_cfuncs("xrCreateInstance", {includes = "openxr/openxr.h"}))
    end)
package_end()
