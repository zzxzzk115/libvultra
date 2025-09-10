package("ktx-local")
    set_kind("library")
    set_description("KTX prebuilt local package")
    set_license("Apache-2.0")

    on_load(function (package)
        package:set("installdir", path.join(os.scriptdir(), package:plat(), package:arch()))
    end)

    on_fetch(function (package)
        local result = {}
        result.links = "ktx"
        result.linkdirs = package:installdir("lib")
        result.includedirs = package:installdir("include")
        result.libfiles = {path.join(result.linkdirs, "ktx.lib")}
        result.dlls = {path.join(package:installdir("bin"), "ktx.dll")}
        return result
    end)
