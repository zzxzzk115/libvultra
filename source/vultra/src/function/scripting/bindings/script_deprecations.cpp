#include "vultra/function/scripting/bindings/script_deprecations.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"

namespace vultra
{
    namespace
    {
        // Lua chunk that wraps old table-function names with warn-once
        // forwarders. Usertype-member deprecations (Transform.rotationEuler,
        // RectTransform.rotationDegrees, UiButton/UiToggle.clicked) warn from
        // their C++ bindings instead and are only recorded in the registry.
        constexpr const char* kDeprecationChunk = R"lua(
local aliases = {
    { "", "vec2", "Vec2" },
    { "", "vec3", "Vec3" },
    { "", "vec4", "Vec4" },
    { "Input", "getKey", "isKeyHeld" },
    { "Input", "getKeyDown", "isKeyPressed" },
    { "Input", "getKeyUp", "isKeyReleased" },
    { "Input", "getKeyRepeat", "isKeyRepeated" },
    { "Input", "getMouseButton", "isMouseButtonHeld" },
    { "Input", "getMouseButtonDown", "isMouseButtonPressed" },
    { "Input", "getMouseButtonUp", "isMouseButtonReleased" },
    { "Input", "getMouseButtonClicks", "mouseButtonClicks" },
    { "Input", "getMousePosition", "mousePosition" },
    { "Input", "getMousePositionFlipY", "mousePositionFlipY" },
    { "Input", "getMousePositionDelta", "mousePositionDelta" },
    { "Input", "getMouseScrollDelta", "mouseScrollDelta" },
    { "Render", "getGaussianSplatSettings", "gaussianSplatSettings" },
    { "Render", "getGaussianSplatFrameStats", "gaussianSplatFrameStats" },
}

local registry = {}

for _, a in ipairs(aliases) do
    local containerName, oldName, newName = a[1], a[2], a[3]
    local container = containerName == "" and _G or _G[containerName]
    if container ~= nil then
        local target = container[newName]
        if target ~= nil then
            local label = containerName == "" and oldName or (containerName .. "." .. oldName)
            local replacement = containerName == "" and newName or (containerName .. "." .. newName)
            local warned = false
            container[oldName] = function(...)
                if not warned then
                    warned = true
                    __vultraWarnDeprecated(label, replacement)
                end
                return target(...)
            end
            registry[label] = replacement
        end
    end
end

-- usertype members deprecated in C++ bindings; recorded here for tooling
registry["Transform.rotationEuler"] = "Transform.rotation"
registry["Transform.setEulerDegrees"] = "Transform.rotation"
registry["RectTransform.rotationDegrees"] = "RectTransform.rotation"
registry["UiButton.clicked"] = "UiButton.onClick"
registry["UiToggle.clicked"] = "UiToggle.onClick"
registry["CameraRef.fovYDegrees"] = "CameraRef.fovY"

__vultraDeprecated = registry
)lua";
    } // namespace

    void registerScriptDeprecations(sol::state& lua)
    {
        lua.set_function("__vultraWarnDeprecated", [](std::string_view oldName, std::string_view newName) {
            script_binding::warnDeprecated(oldName, newName);
        });
        lua.script(kDeprecationChunk, "@script_deprecations");
    }
} // namespace vultra
