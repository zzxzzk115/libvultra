-- One dependency closure drives source files, package requirements and standalone targets.
local requirements = {
    foundation = {},
    random = {"foundation"},
    math = {"foundation"},
    json = {"foundation"},
    input = {"math"},
    input_actions = {"input", "json"},
    image = {"foundation"},
    image_io = {"image"},
    image_metrics = {"image"},
    gpu = {"image"},
    window = {"input"},
    capture = {"gpu"},
    swapchain = {"gpu"},
    render_app = {"swapchain"},
    desktop = {"render_app", "window", "capture"},
    android_entry = {"foundation"},
    android_app = {"render_app", "input", "android_entry", "capture"},
    web_app = {"render_app", "input"},
    graph = {"gpu"},
    graph_json = {"graph", "json"},
    shader_runtime = {"gpu", "json", "asset_source"},
    shader = {"shader_runtime"},
    compute = {"shader"},
    blit = {"shader_runtime"},
    properties = {"math", "json"},
    editor_gui = {"gpu", "input", "properties"},
    asset_source = {"foundation"},
    texture_values = {"foundation"},
    asset_values = {"properties", "texture_values"},
    asset_jobs = {"asset_values"},
    meshlet_build = {"asset_jobs"},
    texture_upload = {"gpu", "texture_values"},
    gpu_scene = {"asset_values", "texture_upload"},
    scene_prepare = {"gpu_scene", "texture_import", "meshlet_build"},
    material_values = {"json"},
    texture_import = {"asset_jobs", "image_io", "asset_source"},
    asset_import = {"texture_import"},
    game_shader_runtime = {"shader_runtime", "material_values", "asset_source"},
    game_shader = {"game_shader_runtime", "shader"},
    shader_material = {"game_shader_runtime"},
    shader_inspector = {"editor_gui", "game_shader", "shader_material"},
    camera = {"input"},
    render_values = {"math"},
    rendering = {"render_values", "graph", "gpu_scene", "shader_material", "blit", "image_io"},
    resource_pack = {"asset_source"},
    benchmark = {"gpu"},
    dialogs = {"window"},
    xr = {"desktop"},
    vgui = {"shader_runtime", "input", "image_io", "asset_source"},
    interactive = {"desktop", "shader", "image_io"},
    tool_import = {"asset_import"},
    tool_shader = {"game_shader"},
    builtin_pack = {"shader", "resource_pack"},
    tools = {"tool_import", "tool_shader"}
}
local root = path.directory(os.scriptdir())

function vultraFeatures()
    local requested = get_config("libvultra_features") or (os.projectdir() == root and "all" or "gpu")
    local selected = {}
    local visiting = {}
    local failure
    local function enable(name)
        if failure then
            return
        end
        if not requirements[name] then
            failure = "Unknown libvultra feature: " .. name
            return
        end
        if visiting[name] then
            failure = "Cyclic libvultra feature dependency: " .. name
            return
        end
        if selected[name] then
            return
        end
        if name == "xr" then
            if not has_config("libvultra_with_openxr") then
                failure = "The xr feature requires libvultra_with_openxr=true"
                return
            end
        end
        visiting[name] = true
        for _, dependency in ipairs(requirements[name]) do
            enable(dependency)
        end
        visiting[name] = nil
        if not failure then
            selected[name] = true
        end
    end
    local count = 0
    for name in requested:gmatch("[^,]+") do
        name = name:trim()
        count = count + 1
        if name == "all" then
            for feature in pairs(requirements) do
                local android = feature == "android_entry" or feature == "android_app"
                local web = feature == "web_app"
                if (not android or is_plat("android")) and (not web or is_plat("wasm")) and
                   (feature ~= "xr" or has_config("libvultra_with_openxr")) then
                    enable(feature)
                end
            end
        else
            enable(name)
        end
    end
    if count == 0 then
        failure = "libvultra_features must name at least one feature"
    end
    if has_config("libvultra_build_examples") then
        local _, required, exampleError = vultraExamples()
        failure = failure or exampleError
        for _, feature in ipairs(required) do
            enable(feature)
        end
    end
    -- Detached GUI windows use real swapchains; offscreen GUI never selects a window backend.
    if selected.editor_gui and selected.window then
        enable("swapchain")
    end
    return selected, failure
end
