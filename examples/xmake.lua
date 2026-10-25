local selected = vultraExamples()
local features = vultraFeatures()
local function has_group(name)
    for id in pairs(selected) do
        if id == name or id:startswith(name .. ".") then
            return true
        end
    end
    return false
end

-- The source and mode table use the same explicit selection, rather than unused link objects.
local function add_mode(group, name, source)
    if selected[group .. "." .. name] then
        add_files(group .. "/" .. source .. ".cpp")
        add_defines("VULTRA_EXAMPLE_" .. name:upper():gsub("-", "_"))
    end
end

local function add_platform_entry()
    add_files("common/entry.cpp")
    if is_plat("android") then
        set_kind("shared")
    elseif is_plat("wasm") then
        set_extension(".html")
        add_ldflags("--shell-file " .. path.join(os.scriptdir(), "common/web_shell.html"), {force = true})
    end
end

if selected["basics.vri"] or selected["basics.graph"] or selected.offline or has_group("ui") then
    target("example-common")
        set_kind("static")
        set_default(false)
        add_deps("vultra", {public = true})
        add_files("common/colored_mesh.cpp")
    target_end()
end

if has_group("basics") then
    target("example-basics")
        set_kind("binary")
        add_deps("vultra")
        if selected["basics.vri"] or selected["basics.graph"] then
            add_deps("example-common")
        end
        add_files("basics/main.cpp")
        add_platform_entry()
        if is_plat("wasm") then
            if selected["basics.vri"] or selected["basics.graph"] then
                local pack = get_config("libvultra_example_pack")
                on_load(function (target)
                    assert(pack ~= "" and os.isfile(pack),
                           "Cook the example VPK on the host; set libvultra_example_pack")
                    target:add("ldflags", "--embed-file " .. path.absolute(pack) .. "@/project.vpk", {force = true})
                end)
            end
        end
        add_mode("basics", "window", "window")
        add_mode("basics", "vri", "vri_triangle")
        add_mode("basics", "graph", "render_graph")
        add_mode("basics", "mesh-shading", "mesh_shader")
        set_rundir("$(projectdir)")
    target_end()
end

if selected.offline then
    target("example-offline")
        set_kind("binary")
        add_deps("example-common")
        add_files("offline/offline.cpp", "offline/color_gain.cpp")
        if is_plat("android") then
            set_kind("shared")
            add_files("offline/android_main.cpp")
        else
            add_files("offline/main.cpp")
        end
        set_rundir("$(projectdir)")
    target_end()
    if features.shader and not is_plat("android", "wasm") then
        target("example-offline-cook")
            set_kind("binary")
            set_default(false)
            add_deps("vultra")
            add_files("offline/cook.cpp")
            set_rundir("$(projectdir)")
        target_end()
    end
end

if has_group("ui") then
    target("example-ui")
        set_kind("binary")
        add_deps("example-common", "vultra")
        add_files("ui/main.cpp")
        set_rundir("$(projectdir)")
    target_end()
end
