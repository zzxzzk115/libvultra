if is_plat("android") then
    target("example-android-app-runtime")
        set_kind("shared")
        set_basename("vultra_android_runtime")

        add_files("native/vultra_android_runtime.cpp")
        add_deps("vultra")
        add_syslinks("android", "log")

        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-android-app-runtime")
end
