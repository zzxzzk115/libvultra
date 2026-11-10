package("directxtex-macos")
    set_base("directxtex")
    add_deps("directxmath-macos 2024.02", "directx-headers-macos")
    on_install("macosx", function (package)
        import("package.tools.cmake").install(package, {
            "-DBUILD_TOOLS=OFF", "-DBUILD_SAMPLE=OFF", "-DBUILD_DX11=OFF", "-DBUILD_DX12=OFF",
            "-DBC_USE_OPENMP=OFF", "-DENABLE_OPENEXR_SUPPORT=OFF"
        })
    end)
package_end()
