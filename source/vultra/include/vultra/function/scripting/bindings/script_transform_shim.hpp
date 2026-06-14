#pragma once

// Shim declarations for the Lua `Transform` usertype (handles both 3D
// TransformComponent and 2D RectTransformComponent). Generated into the
// `transform` area; bodies in script_transform_shim.cpp own the rect-vs-3D
// branching and euler/quat conversion.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"

namespace vultra
{
    struct VBIND_USERTYPE(name = Transform, handle = ScriptTransformRef, area = transform,
                          accessor = transform) TransformUsertype
    {
    };

    VBIND_PROPERTY(usertype = Transform, name = position, set = transformSetPosition)
    ScriptVec3 transformGetPosition(ScriptContext& ctx, const ScriptTransformRef& self);
    void transformSetPosition(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value);
    VBIND_PROPERTY(usertype = Transform, name = scale, set = transformSetScale)
    ScriptVec3 transformGetScale(ScriptContext& ctx, const ScriptTransformRef& self);
    void transformSetScale(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value);
    VBIND_PROPERTY(usertype = Transform, name = rotation, set = transformSetRotation)
    ScriptVec3 transformGetRotation(ScriptContext& ctx, const ScriptTransformRef& self);
    void transformSetRotation(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value);
    // deprecated alias of `rotation` (warn-once)
    VBIND_PROPERTY(usertype = Transform, name = rotationEuler, set = transformSetRotationEuler)
    ScriptVec3 transformGetRotationEuler(ScriptContext& ctx, const ScriptTransformRef& self);
    void transformSetRotationEuler(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value);

    VBIND_FN(usertype = Transform, name = translate, body = shim)
    void transformTranslate(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& delta);
    VBIND_FN(usertype = Transform, name = setEulerDegrees, body = shim)
    void transformSetEulerDegrees(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value);
    VBIND_FN(usertype = Transform, name = lookAt, body = shim)
    void transformLookAt(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& target);
} // namespace vultra
