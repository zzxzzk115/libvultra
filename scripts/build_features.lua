-- One dependency closure drives source files, package requirements and standalone targets.
local requirements = {
    foundation = {},
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
    swapchain = {"gpu", "window"},
    desktop = {"swapchain", "capture"},
    graph = {"gpu"},
    graph_json = {"graph", "json"},
    shader = {"gpu", "json"},
    blit = {"shader"},
    properties = {"math", "json"},
    editor_gui = {"gpu", "input", "properties"},
    asset_source = {"foundation"},
    project = {"asset_source", "json"},
    asset_values = {"properties"},
    material_values = {"json"},
    texture_import = {"asset_values", "image_io", "asset_source"},
    asset_import = {"texture_import"},
    game_shader = {"shader", "material_values", "asset_source"},
    shader_inspector = {"editor_gui", "game_shader", "project"},
    camera = {"input"},
    rendering = {"graph", "game_shader", "texture_import", "blit"},
    world = {"asset_values", "material_values", "project", "input_actions"},
    scene_rendering = {"world", "rendering", "asset_import"},
    asset_export = {"world", "game_shader", "asset_import"},
    resource_pack = {"asset_source"},
    benchmark = {"gpu"},
    dialogs = {"window"},
    api_scene = {"world"},
    api_ui = {"editor_gui"},
    api_host = {"api_scene"},
    native_sdk = {"foundation"},
    native_plugin = {"api_host", "native_sdk"},
    script_host = {"api_host"},
    script_native = {"script_host", "native_plugin"},
    script_lua = {"script_host"},
    script_csharp = {"script_host"},
    scripting = {"script_native", "script_lua", "script_csharp", "api_ui"},
    xr = {"desktop"},
    vgui = {"shader", "input", "image_io", "asset_source"},
    interactive = {"desktop", "shader", "image_io"},
    engine = {"scene_rendering", "resource_pack"},
    research_quality = {"image_metrics", "math"},
    research_graph = {"graph", "json"},
    research_camera = {"math", "json"},
    research_project = {"project"},
    research_reference = {"rendering"},
    research_experiment = {"scene_rendering", "graph_json", "camera", "research_reference"},
    research_api = {"rendering", "project", "native_sdk"},
    research_stereo = {"rendering", "graph_json", "camera", "research_camera", "xr"},
    research = {"research_quality", "research_graph", "research_camera", "research_project", "research_reference",
                "research_experiment", "research_api"},
    research_host = {"research_experiment", "research_graph", "resource_pack", "image_metrics", "script_host"},
    tool_import = {"asset_import"},
    tool_shader = {"game_shader"},
    tool_pack = {"asset_export"},
    tool_sdk = {"gpu", "native_sdk"},
    builtin_pack = {"tool_pack", "tool_sdk", "resource_pack"},
    tool_batch = {"research_experiment", "research_graph", "benchmark", "capture", "image_metrics", "builtin_pack",
                  "script_host"},
    tool_python = {"research_host", "builtin_pack"},
    tools = {"tool_import", "tool_shader", "tool_pack", "tool_sdk", "tool_batch", "tool_python"},
    runtime = {"engine", "desktop", "camera", "vgui", "script_host", "builtin_pack"},
    editor = {"engine", "desktop", "camera", "api_ui", "native_plugin", "shader_inspector", "dialogs", "benchmark", "asset_export",
              "research", "builtin_pack"}
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
            for _, feature in ipairs({"editor", "runtime", "tools", "scripting"}) do
                enable(feature)
            end
            if has_config("libvultra_with_openxr") then
                enable("research_stereo")
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
    if selected.editor and selected.xr then
        enable("research_stereo")
    end
    return selected, failure
end
