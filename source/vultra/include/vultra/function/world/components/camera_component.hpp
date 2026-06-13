#pragma once

#include "vultra/core/base/lua_annotations.hpp"
#include "vultra/function/world/components/layer_component.hpp"

#include <glm/vec4.hpp>

#include <cstdint>
#include <string>

namespace vultra
{
    struct VLUA_CLASS(name = CameraRef, ref = ScriptCameraRef, accessor = camera) CameraComponent
    {
        VLUA_FIELD() bool primary {false};

        // 0 = perspective, 1 = orthographic. Kept numeric for simple v1 scene serialization.
        VLUA_FIELD() uint32_t projection {0};

        // C++ field name == Lua property name by design; the Lua-side legacy
        // alias lives for one release (doc/lua_api_design.md section 7)
        VLUA_FIELD(deprecated = fovYDegrees) float fovY {60.0f};
        VLUA_FIELD() float orthographicHeight {10.0f};
        VLUA_FIELD() float zNear {0.1f};
        VLUA_FIELD() float zFar {1000.0f};

        // 0 = solid color, 1 = scene environment skybox.
        VLUA_FIELD() uint32_t clearMode {0};
        VLUA_FIELD() glm::vec4 clearColor {0.02f, 0.025f, 0.035f, 1.0f};
        VLUA_FIELD() int priority {0};
        VLUA_FIELD() uint32_t cullingMask {kRenderLayerAllMask};

        VLUA_FIELD() std::string rendererKey {"universal"};
    };
} // namespace vultra
