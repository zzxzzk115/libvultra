#pragma once

#include "vultra/core/base/lua_annotations.hpp"
#include "vultra/function/asset/builtin_assets.hpp"

#include <glm/vec3.hpp>

namespace vultra
{
    struct VLUA_CLASS(name = Environment, ref = ScriptEnvironmentRef, accessor = environment) EnvironmentComponent
    {
        VLUA_FIELD() bool active {true};

        VLUA_FIELD() CoreUUID skybox {builtinCitrusOrchardSkyTextureUuid()};

        VLUA_FIELD() glm::vec3 ambientColor {0.15f};
        VLUA_FIELD() float     ambientIntensity {1.0f};

        VLUA_FIELD() bool      enableIBL {false};
        VLUA_FIELD() glm::vec3 iblColor {0.04f, 0.045f, 0.05f};
        VLUA_FIELD() float     iblIntensity {1.0f};
    };
} // namespace vultra
