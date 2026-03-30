if is_plat("android") then
    target("example-android-app-runtime")
        set_kind("shared")
        set_basename("vultra_android_runtime")
        add_files("runtime_main.cpp")
        add_deps("vultra")
        add_syslinks("android", "log")
        on_load(function (target)
            local user_home = os.getenv("USERPROFILE") or os.getenv("HOME") or ""
            local gradle_home = os.getenv("GRADLE_USER_HOME") or path.join(user_home, ".gradle")
            local arch = target:arch()
            local game_activity_arch = "android." .. arch
            local libs = os.files(path.join(gradle_home,
                                            "caches",
                                            "**",
                                            "games-activity-*",
                                            "prefab",
                                            "modules",
                                            "game-activity",
                                            "libs",
                                            game_activity_arch,
                                            "libgame-activity.a"))
            if #libs > 0 then
                target:add("links", libs[1])
            end
        end)
        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/example-android-app-runtime")
end
