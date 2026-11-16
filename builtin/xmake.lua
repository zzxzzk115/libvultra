-- The library may be a subproject: its sources are next to this script, the pack goes to the project build.
local library_root = path.directory(os.scriptdir())
-- The graphics APIs the embedded archive is cooked for.
local built_apis = table.concat(vultraGraphicsApis() or {}, ",")

target("vultra-builtin-pack")
    set_kind("phony")
    set_default(false)
    set_policy("build.fence", true)
    add_deps("vultra-builtins")
    on_build(function (target)
        import("core.project.depend")
        local output = path.join(os.projectdir(), "build", ".tmp", "runtime-builtin.vpk")
        local pack = target:dep("vultra-builtins")
        local files = table.join(os.files(path.join(library_root, "builtin", "shaders", "**")),
                                 os.files(path.join(library_root, "external", "openpbr", "**")),
                                 {path.join(library_root, "external", "pcg_basic", "LICENSE.txt"),
                                  path.join(library_root, "external", "pcg_basic", "README.md")},
                                 os.files(path.join(library_root, "build", "sdk", "**")),
                                 {pack:targetfile()})
        table.sort(files)
        -- Configurations of one checkout share the embedded archive; its shaders are compiled for the graphics APIs
        -- of the build, recorded next to it, so a build for other APIs cooks it again.
        local api = built_apis
        local stamp = output .. ".api"
        depend.on_changed(function ()
            local candidate = output .. ".new"
            os.mkdir(path.directory(output))
            os.tryrm(candidate)
            os.execv(pack:targetfile(), {library_root, candidate})
            local changed = not os.isfile(output) or io.readfile(output) ~= io.readfile(candidate)
            if changed then
                os.mv(candidate, output)
                local embed = path.join(library_root, "builtin", is_plat("windows") and "builtin_pack.rc" or "builtin_pack.S")
                -- Recompile the .S/.rc source only when the embedded archive changes.
                io.writefile(embed, io.readfile(embed))
            else
                os.tryrm(candidate)
            end
            io.writefile(stamp, api)
        end, {dependfile = target:dependfile(output), files = files,
              values = {get_config("plat"), get_config("arch"), get_config("mode"), api},
              changed = not os.isfile(output) or not os.isfile(stamp) or io.readfile(stamp) ~= api})
    end)
target_end()
