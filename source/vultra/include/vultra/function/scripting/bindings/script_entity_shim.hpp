#pragma once

// Shim declarations for the Lua `Entity` usertype.
//
// Entity exposes only the always-on members directly: valid / id / name /
// active / visible / transform, plus hierarchy. Every other component is reached
// through the Unity-style generic API (addComponent / getComponent /
// removeComponent / hasComponent), keyed by the `Component` enum. Those four
// methods and the `Component` token table are registered by the hand-written raw
// hook entityRegisterComponentApi (script_entity_shim.cpp).

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"

#include <sol/sol.hpp>

#include <cstdint>
#include <string>

namespace vultra
{
    // Component tokens for entity:addComponent/getComponent/removeComponent/hasComponent.
    // Lua side: Component.Transform, Component.RigidBody, ... (PascalCase via stripE).
    enum class VBIND_ENUM(name = Component, stripE, module = World) ScriptComponentType : int
    {
        eTransform,
        eRigidBody,
        eCamera,
        eLight,
        eMesh,
        eBoxShape,
        eSphereShape,
        eCapsuleShape,
        eCylinderShape,
        eAnimator,
        eAudioSource,
        eAudioListener,
        eEnvironment,
        eParticleEmitter,
        eReflectionProbe,
        eRectTransform,
        eUiButton,
        eUiToggle,
        eUiSlider,
        eUiProgressBar,
        eNavAgent,
    };

    struct VBIND_USERTYPE(name = Entity, handle = ScriptEntity, area = entity) EntityUsertype
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

    // Registers the generic component API (addComponent/getComponent/removeComponent/hasComponent)
    // onto the Entity usertype. Runs after the Entity usertype is built in the `entity` area.
    VBIND_RAW(area = entity)
    void entityRegisterComponentApi(sol::state& lua, ScriptContext& ctx);
} // namespace vultra
