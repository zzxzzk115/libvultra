#pragma once

// Shim declarations for the Lua `Entity` usertype. Generated into the `entity`
// area; postRegister calls applyGeneratedEntityAccessors (the legacy component
// accessors: camera/light/etc.). Bodies in script_entity_shim.cpp.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/bindings/script_generated_binding.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"

#include <cstdint>
#include <string>

namespace vultra
{
    struct VBIND_USERTYPE(name = Entity, handle = ScriptEntity, area = entity,
                          postRegister = applyGeneratedEntityAccessors) EntityUsertype
    {
    };

    VBIND_PROPERTY(usertype = Entity, name = valid)
    bool entityGetValid(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = id)
    std::uint32_t entityGetId(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = name, set = entitySetName)
    std::string entityGetName(ScriptContext& ctx, const ScriptEntity& self);
    void entitySetName(ScriptContext& ctx, const ScriptEntity& self, const std::string& value);
    VBIND_PROPERTY(usertype = Entity, name = active, set = entitySetActive)
    bool entityGetActive(ScriptContext& ctx, const ScriptEntity& self);
    void entitySetActive(ScriptContext& ctx, const ScriptEntity& self, const bool& value);
    VBIND_PROPERTY(usertype = Entity, name = visible, set = entitySetVisible)
    bool entityGetVisible(ScriptContext& ctx, const ScriptEntity& self);
    void entitySetVisible(ScriptContext& ctx, const ScriptEntity& self, const bool& value);

    VBIND_PROPERTY(usertype = Entity, name = transform)
    ScriptTransformRef entityTransform(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = rectTransform)
    ScriptRectTransformRef entityRectTransform(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = ui)
    ScriptUiRef entityUi(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = uiButton)
    ScriptUiButtonRef entityUiButton(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = uiToggle)
    ScriptUiToggleRef entityUiToggle(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = uiSlider)
    ScriptUiSliderRef entityUiSlider(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = uiProgressBar)
    ScriptUiProgressBarRef entityUiProgressBar(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = rigidBody)
    ScriptRigidBodyRef entityRigidBody(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = mesh)
    ScriptMeshRef entityMesh(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_PROPERTY(usertype = Entity, name = animator)
    ScriptAnimatorRef entityAnimator(ScriptContext& ctx, const ScriptEntity& self);

    VBIND_FN(usertype = Entity, name = destroy, body = shim)
    void entityDestroy(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = parent, body = shim)
    ScriptEntity entityParent(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = firstChild, body = shim)
    ScriptEntity entityFirstChild(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = nextSibling, body = shim)
    ScriptEntity entityNextSibling(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = setParent, body = shim)
    void entitySetParent(ScriptContext& ctx, const ScriptEntity& self, const ScriptEntity& parent);
    VBIND_FN(usertype = Entity, name = hasRigidBody, body = shim)
    bool entityHasRigidBody(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = hasMesh, body = shim)
    bool entityHasMesh(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = hasAnimator, body = shim)
    bool entityHasAnimator(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = hasRectTransform, body = shim)
    bool entityHasRectTransform(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = hasUiButton, body = shim)
    bool entityHasUiButton(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = hasUiToggle, body = shim)
    bool entityHasUiToggle(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = hasUiSlider, body = shim)
    bool entityHasUiSlider(ScriptContext& ctx, const ScriptEntity& self);
    VBIND_FN(usertype = Entity, name = hasUiProgressBar, body = shim)
    bool entityHasUiProgressBar(ScriptContext& ctx, const ScriptEntity& self);
} // namespace vultra
