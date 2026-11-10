package("moltenvk-vultra")
    set_homepage("https://github.com/KhronosGroup/MoltenVK")
    set_description("macOS MoltenVK runtime with the buffer-array length expression fix")
    set_license("Apache-2.0")
    set_kind("binary")
    add_urls("https://github.com/KhronosGroup/MoltenVK/archive/81541f6488c5feeace53867fc8ab612e578766da.tar.gz")
    add_versions("1.2.11", "9922526b8ec4cf122c2b26f17d6ead19ec479ebea720cc03b1d786a50c4a30db")
    add_resources("1.2.11", "cross",
        "https://github.com/KhronosGroup/SPIRV-Cross/archive/65d7393430f6c7bb0c20b6d53250fe04847cc2ae.tar.gz",
        "03b517a3d089aee034d5e1ab2d6064e5c2cdb72d250287998892526e2a390055")
    add_resources("1.2.11", "headers",
        "https://github.com/KhronosGroup/Vulkan-Headers/archive/29f979ee5aa58b7b005f805ea8df7a855c39ff37.tar.gz",
        "dfc96c55d7e481c42b917706ff52205d47c0f0957c8c2a60766ee781f16d18bd")
    add_resources("1.2.11", "cereal",
        "https://github.com/USCiLab/cereal/archive/51cbda5f30e56c801c07fe3d3aba5d7fb9e6cca4.tar.gz",
        "26361b539fe50eee308b564faa2742166d2922a7ab0bd4870ac55708581228c8")
    add_patches("1.2.11", "glslang_public_headers.patch", "e695f6b9191050a51b27f4e4b56770835707406041d2e7a2de3218258e157125")
    add_deps("cmake", "glslang 1.4.335+0", "spirv-tools 1.4.335+0", {system = false})

    on_load(function (package)
        package:mark_as_pathenv("VK_DRIVER_FILES")
        package:addenv("VK_DRIVER_FILES", "share/vulkan/icd.d/MoltenVK_icd.json")
    end)

    on_fetch(function (package)
        if os.isfile(path.join(package:installdir("lib"), "libMoltenVK.dylib")) and
           os.isfile(path.join(package:installdir("share", "vulkan", "icd.d"), "MoltenVK_icd.json")) then
            return {bindirs = {package:installdir("lib")}}
        end
    end)

    on_install("macosx", function (package)
        import("devel.git")
        local crossResource = path.join(package:resourcedir("cross"),
                                "SPIRV-Cross-65d7393430f6c7bb0c20b6d53250fe04847cc2ae")
        local cross = path.join(os.curdir(), "External", "SPIRV-Cross")
        os.tryrm(cross)
        os.cp(crossResource, cross)
        local patch = path.join(package:scriptdir(), "spirv_cross_buffer_sizes.patch")
        assert(hash.sha256(patch) == "6e3ad2aed10751634981e8257f67eceb0eccd80508f34e7dee1fa52aaf5443e2",
               "MoltenVK buffer-size patch checksum mismatch")
        os.vrunv("git", {"apply", "--check", patch}, {curdir = cross})
        git.apply(patch, {repodir = cross})
        os.cp(path.join(package:scriptdir(), "CMakeLists.txt"), "CMakeLists.txt")
        import("package.tools.cmake").install(package, {
            "-DCMAKE_BUILD_TYPE=Release",
            "-DMVK_SOURCE_DIR=" .. os.curdir(),
            "-DCROSS_SOURCE_DIR=" .. cross,
            "-DHEADERS_SOURCE_DIR=" .. path.join(package:resourcedir("headers"),
                "Vulkan-Headers-29f979ee5aa58b7b005f805ea8df7a855c39ff37"),
            "-DCEREAL_SOURCE_DIR=" .. path.join(package:resourcedir("cereal"),
                "cereal-51cbda5f30e56c801c07fe3d3aba5d7fb9e6cca4"),
            "-Dglslang_DIR=" .. package:dep("glslang"):installdir("lib", "cmake", "glslang"),
            "-DSPIRV-Tools_DIR=" .. package:dep("spirv-tools"):installdir("lib", "cmake", "SPIRV-Tools")
        })
        io.writefile(path.join(package:installdir("share", "vulkan", "icd.d"), "MoltenVK_icd.json"), [[
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../../lib/libMoltenVK.dylib",
        "api_version": "1.2.0",
        "is_portability_driver": true
    }
}
]])
        local licenses = package:installdir("share", "licenses", "moltenvk-vultra")
        os.cp("LICENSE", path.join(licenses, "MoltenVK-LICENSE"))
        os.cp(path.join(crossResource, "LICENSE"), path.join(licenses, "SPIRV-Cross-LICENSE"))
        os.cp(path.join(package:resourcedir("headers"),
            "Vulkan-Headers-29f979ee5aa58b7b005f805ea8df7a855c39ff37", "LICENSE.md"),
            path.join(licenses, "Vulkan-Headers-LICENSE.md"))
        os.cp(path.join(package:resourcedir("cereal"),
            "cereal-51cbda5f30e56c801c07fe3d3aba5d7fb9e6cca4", "LICENSE"),
            path.join(licenses, "cereal-LICENSE"))
    end)

    on_test(function (package)
        assert(os.isfile(path.join(package:installdir("lib"), "libMoltenVK.dylib")))
        assert(os.isfile(path.join(package:installdir("share", "vulkan", "icd.d"), "MoltenVK_icd.json")))
    end)
package_end()
