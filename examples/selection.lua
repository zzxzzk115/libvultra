-- These are application requirements, not higher-level presets for every example.
local examples = {
    {"basics.window", {"desktop", "image_io"}},
    {"basics.vri", {"desktop", "shader", "image_io"}},
    {"basics.graph", {"desktop", "shader", "graph", "image_io"}},
    {"basics.mesh-shading", {"desktop", "shader", "image_io"}},
    {"offline", {"shader", "graph_json", "blit", "image_io"}},
    {"ui", {"desktop", "editor_gui", "vgui"}},
    {"ui.native", {"desktop", "api_ui", "vgui", "native_plugin"}},
    {"scene.helmet", {"desktop", "rendering", "tool_import", "camera", "editor_gui", "dialogs"}},
    {"scene.debug", {"desktop", "rendering", "tool_import", "camera", "editor_gui", "dialogs"}},
    {"scene.sponza", {"desktop", "rendering", "tool_import", "camera", "editor_gui", "dialogs"}},
    {"scene.sponza-mesh-shading", {"desktop", "rendering", "tool_import", "camera", "editor_gui", "dialogs"}},
    {"ray.triangle", {"desktop", "blit", "asset_values", "camera", "editor_gui", "image_io"}},
    {"ray.cornell", {"desktop", "blit", "asset_import", "camera", "editor_gui", "image_io", "tool_import"}},
    {"ray.query", {"desktop", "shader", "asset_import", "camera", "editor_gui", "image_io", "tool_import"}},
    {"xr.triangle", {"xr", "blit", "editor_gui", "image_io"}},
    {"xr.sponza", {"xr", "rendering", "asset_import", "camera", "editor_gui", "tool_import"}},
    {"shader", {"desktop", "rendering", "camera", "shader_inspector", "tool_shader"}},
    {"research", {"desktop", "scene_rendering", "camera", "editor_gui", "graph_json", "benchmark", "image_metrics",
                  "tool_import"}},
    {"scripting", {"desktop", "shader", "image_io", "scripting"}}
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
            -- 'ui' selects three UI styles; its native ABI demonstration is an explicit addition.
            local group = name ~= "ui" and id:startswith(name .. ".")
            if name == "all" or id == name or group then
                found = true
                if id:startswith("xr.") and not has_config("libvultra_with_openxr") then
                    if name ~= "all" then
                        return selected, required, "XR examples require libvultra_with_openxr=true"
                    end
                else
                    selected[id] = true
                    table.join2(required, example[2])
                end
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
