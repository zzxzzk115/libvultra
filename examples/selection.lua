-- These are application requirements, not higher-level presets for every example.
local app = "desktop"
if is_plat("android") then
    app = "android_app"
elseif is_plat("wasm") then
    app = "web_app"
end
local shader = is_plat("android", "wasm") and "shader_runtime" or "shader"
local examples = {
    {"basics.window", {app, "image_io"}},
    {"basics.vri", {app, shader, "image_io", "asset_source"}},
    {"basics.graph", {app, shader, "graph", "image_io", "asset_source"}},
    {"basics.mesh-shading", {app, "shader", "image_io"}},
    {"offline", {is_plat("android") and "shader_runtime" or "shader", "graph_json", "blit", "image_io", "asset_source",
                  is_plat("android") and "android_entry" or "foundation"}},
    {"ui", {app, shader, "editor_gui", "vgui"}}
}

function vultraExamples()
    local selected = {}
    local required = {}
    local requested = get_config("libvultra_examples") or "all"
    local count = 0
    for name in requested:gmatch("[^,]+") do
        name = name:trim()
        count = count + 1
        local found = false
        for _, example in ipairs(examples) do
            local id = example[1]
            local group = id:startswith(name .. ".")
            if name == "all" or id == name or group then
                found = true
                selected[id] = true
                table.join2(required, example[2])
            end
        end
        if not found then
            return selected, required, "Unknown Vultra example selection: " .. name
        end
    end
    if count == 0 then
        return selected, required, "libvultra_examples must name at least one example"
    end
    return selected, table.unique(required)
end
