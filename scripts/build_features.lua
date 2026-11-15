-- Each feature is one source module (source/<name>); its dependencies are selected automatically.
local requirements = {
    core = {},
    platform = {"core"},
    gpu = {"core", "platform"},
    shader = {"gpu", "assets"},
    graph = {"gpu"},
    assets = {"core", "platform"},
    render = {"graph", "shader", "assets"},
    ui = {"gpu", "platform"},
    app = {"gpu", "platform"},
    xr = {"gpu"}
}
local root = path.directory(os.scriptdir())

-- Why xr cannot be built in this configuration, or nil. The xr module creates its OpenXR session through
-- XR_KHR_vulkan_enable2, the only OpenXR graphics binding it implements; OpenXR has no WebGPU binding, and
-- browsers offer XR through WebXR instead.
function vultraXrError()
    if not has_config("libvultra_with_openxr") then
        return "The xr feature requires libvultra_with_openxr=true"
    end
    local api = get_config("libvultra_graphics_api") or (is_plat("wasm") and "webgpu" or "vulkan")
    if api ~= "vulkan" then
        return "The xr feature uses OpenXR's Vulkan binding; libvultra_graphics_api=" .. api .. " has no XR support"
    end
end

function vultraFeatures()
    local requested = get_config("libvultra_features") or (os.projectdir() == root and "all" or "gpu")
    local selected = {}
    local failure
    local function enable(name)
        if failure or selected[name] then
            return
        end
        if not requirements[name] then
            failure = "Unknown libvultra feature: " .. name
            return
        end
        if name == "xr" and vultraXrError() then
            failure = vultraXrError()
            return
        end
        selected[name] = true
        for _, dependency in ipairs(requirements[name]) do
            enable(dependency)
        end
    end
    local count = 0
    for name in requested:gmatch("[^,]+") do
        name = name:trim()
        count = count + 1
        if name == "all" then
            for feature in pairs(requirements) do
                if feature ~= "xr" or not vultraXrError() then
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
    return selected, failure
end
