package("directx-headers-macos")
    set_base("directx-headers")
    add_includedirs("include", "include/wsl/stubs")
    on_install("macosx", function (package)
        import("package.tools.cmake").install(package, {
            "-DDXHEADERS_BUILD_TEST=OFF",
            "-DDXHEADERS_BUILD_GOOGLE_TEST=OFF"
        })
    end)
package_end()
