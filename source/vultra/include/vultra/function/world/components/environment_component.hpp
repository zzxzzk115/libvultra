#pragma once

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/asset/builtin_assets.hpp"

#include <glm/vec3.hpp>

namespace vultra
{
    struct VBIND_USERTYPE(name = Environment, handle = ScriptEnvironmentRef, accessor = environment) EnvironmentComponent
    {
        VBIND_FIELD() bool active {true};

        VBIND_FIELD() CoreUUID skybox {builtinCitrusOrchardSkyTextureUuid()};

        VBIND_FIELD() glm::vec3 ambientColor {0.15f};
        VBIND_FIELD() float     ambientIntensity {1.0f};

        VBIND_FIELD() bool      enableIBL {false};
        VBIND_FIELD() glm::vec3 iblColor {0.04f, 0.045f, 0.05f};
        VBIND_FIELD() float     iblIntensity {1.0f};
    };
} // namespace vultra
