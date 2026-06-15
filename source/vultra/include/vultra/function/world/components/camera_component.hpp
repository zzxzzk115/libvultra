#pragma once

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/world/components/layer_component.hpp"

#include <glm/vec4.hpp>

#include <cstdint>
#include <string>

namespace vultra
{
    struct VBIND_USERTYPE(name = CameraRef, handle = ScriptCameraRef) CameraComponent
    {
        VBIND_FIELD() bool primary {false};

        // 0 = perspective, 1 = orthographic. Kept numeric for simple v1 scene serialization.
        VBIND_FIELD() uint32_t projection {0};

        // C++ field name == Lua property name by design; the Lua-side legacy
        // alias lives for one release (doc/lua_api_design.md section 7)
        VBIND_FIELD(deprecated = fovYDegrees) float fovY {60.0f};
        VBIND_FIELD() float orthographicHeight {10.0f};
        VBIND_FIELD() float zNear {0.1f};
        VBIND_FIELD() float zFar {1000.0f};

        // 0 = solid color, 1 = scene environment skybox.
        VBIND_FIELD() uint32_t clearMode {0};
        VBIND_FIELD() glm::vec4 clearColor {0.02f, 0.025f, 0.035f, 1.0f};
        VBIND_FIELD() int priority {0};
        VBIND_FIELD() uint32_t cullingMask {kRenderLayerAllMask};

        VBIND_FIELD() std::string rendererKey {"universal"};
    };
} // namespace vultra
