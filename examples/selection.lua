-- Modules each example uses directly; vultraFeatures() adds their dependencies.
local examples = {
    {"basics.window", {"app"}},
    {"basics.vri", {"app", "shader", "assets"}},
    {"basics.graph", {"app", "shader", "graph", "assets"}},
    {"basics.mesh-shading", {"app", "shader"}},
    {"offline", {"shader", "graph", "assets", "render"}},
    {"ui", {"app", "shader", "ui"}},
    {"xr", {"xr", "shader", "assets"}}
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
            -- "all" leaves the XR example out of a build without OpenXR; naming it reports the missing module.
            local all = name == "all" and (id ~= "xr" or has_config("libvultra_with_openxr"))
            if all or id == name or group then
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
