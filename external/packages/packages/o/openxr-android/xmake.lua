package("openxr-android")
    set_homepage("https://github.com/KhronosGroup/OpenXR-SDK")
    set_description("Android OpenXR loader with safe runtime probing")
    set_license("Apache-2.0")
    add_urls("https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-$(version).tar.gz")
    add_versions("1.1.49", "74e9260a1876b0540171571a09bad853302ec68a911200321be8b0591ca94111")
    add_patches("1.1.49", "android-runtime-probe.patch", "5c8ba1ccdc854a335eb573662eb5ec8a2b904b64fb6d38543fccb50bc0616d01")
    add_deps("cmake", "python 3.x", {kind = "binary"})
    add_deps("jsoncpp", "egl-headers")
    add_syslinks("log", "android")

    on_install("android", function (package)
        io.replace("src/CMakeLists.txt", "set(CMAKE_POSITION_INDEPENDENT_CODE ON)", "", {plain = true})
        os.vcp("include", package:installdir())
        import("package.tools.cmake").install(package, {
            "-DBUILD_CONFORMANCE_TESTS=OFF",
            "-DBUILD_TESTS=OFF",
            "-DOPENXR_DEBUG_POSTFIX=''",
            "-DBUILD_WITH_SYSTEM_JSONCPP=ON",
            "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"),
            "-DBUILD_SHARED_LIBS=OFF",
            "-DDYNAMIC_LOADER=OFF",
            "-DBUILD_API_LAYERS=OFF",
            "-DINSTALL_TO_ARCHITECTURE_PREFIXES=ON",
        })
    end)

    on_test(function (package)
        assert(package:has_cfuncs("xrCreateInstance", {includes = "openxr/openxr.h"}))
    end)
package_end()
