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

-- The graphics APIs a platform builds when libvultra_graphics_apis names none, and the ones it can build, both in
-- the order Device::create() tries them when a program names none: Vulkan first where it exists, Metal first on
-- macOS, WebGPU on the Web.
local function graphicsApiPlatform()
    if is_plat("windows") then
        return {"vulkan", "d3d12"}, {"vulkan", "d3d12", "webgpu"}
    elseif is_plat("macosx") then
        return {"metal"}, {"metal", "vulkan", "webgpu"}
    elseif is_plat("wasm") then
        return {"webgpu"}, {"webgpu", "opengl"}
    elseif is_plat("android") then
        return {"vulkan"}, {"vulkan"}
    end
    return {"vulkan"}, {"vulkan", "webgpu"}
end

-- The graphics APIs of this build in automatic order, or nil and why the selection is invalid. OpenGL is built only
-- for the Web (WebGL 2) until the kernel runs on VRI's desktop and ES OpenGL backends.
function vultraGraphicsApis()
    local defaults, supported = graphicsApiPlatform()
    local requested = get_config("libvultra_graphics_apis")
    if not requested or requested == "" then
        return defaults
    end
    local wanted = {}
    for name in requested:gmatch("[^,]+") do
        name = name:trim()
        if not table.contains(supported, name) then
            if name == "opengl" then
                return nil, "libvultra does not run on OpenGL on this platform yet; OpenGL is built for the Web only"
            end
            return nil, "libvultra_graphics_apis: " .. name .. " is not available on " .. get_config("plat") ..
                        " (available: " .. table.concat(supported, ", ") .. ")"
        end
        wanted[name] = true
    end
    local apis = {}
    for _, name in ipairs(supported) do
        if wanted[name] then
            table.insert(apis, name)
        end
    end
    if #apis == 0 then
        return nil, "libvultra_graphics_apis must name at least one graphics API"
    end
    return apis
end

function vultraHasGraphicsApi(name)
    return table.contains(vultraGraphicsApis() or {}, name)
end

-- Why xr cannot be built in this configuration, or nil. The xr module creates its OpenXR session through
-- XR_KHR_vulkan_enable2, the only OpenXR graphics binding it implements; OpenXR has no WebGPU binding, and
-- browsers offer XR through WebXR instead.
function vultraXrError()
    if not has_config("libvultra_with_openxr") then
        return "The xr feature requires libvultra_with_openxr=true"
    end
    if not vultraHasGraphicsApi("vulkan") then
        return "The xr feature uses OpenXR's Vulkan binding; libvultra_graphics_apis has no vulkan"
    end
end

function vultraFeatures()
    local requested = get_config("libvultra_features") or (os.projectdir() == root and "all" or "gpu")
    local selected = {}
    local _, failure = vultraGraphicsApis()
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
