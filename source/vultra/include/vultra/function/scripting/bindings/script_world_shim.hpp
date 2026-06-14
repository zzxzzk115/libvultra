#pragma once

// Shim declarations for the Lua `World` namespace, the `Mesh` usertype, and the
// `Layer` constant table. Generated into the `world` area; bodies in
// script_world_shim.cpp own the entity lifecycle, name queries, component
// add/remove, and material-override mutation.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/world/components/mesh_component.hpp"

#include <sol/sol.hpp>

#include <cstdint>
#include <string>

namespace vultra
{
    struct VBIND_MODULE(name = World, area = world, service = worldService) WorldModule
    {
    };
    struct VBIND_USERTYPE(name = Mesh, handle = ScriptMeshRef, area = world,
                          component = MeshComponent, accessor = mesh) MeshUsertype
    {
    };

    // --- World namespace ---
    VBIND_FN(module = World, name = create, body = shim)
    ScriptEntity worldCreate(ScriptContext& ctx, sol::optional<std::string> name);
    VBIND_FN(module = World, name = destroy, body = shim)
    void worldDestroy(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = count, body = shim)
    std::uint32_t worldCount(ScriptContext& ctx);
    VBIND_FN(module = World, name = findByName, body = shim)
    ScriptEntity worldFindByName(ScriptContext& ctx, const std::string& name);
    VBIND_FN(module = World, name = findByNamePrefix, body = shim)
    sol::table worldFindByNamePrefix(ScriptContext& ctx, sol::this_state luaState, const std::string& prefix);
    VBIND_FN(module = World, name = entities, body = shim)
    sol::table worldEntities(ScriptContext& ctx, sol::this_state luaState);

    VBIND_FN(module = World, name = addRigidBody, body = shim)
    ScriptRigidBodyRef worldAddRigidBody(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = removeRigidBody, body = shim)
    bool worldRemoveRigidBody(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = addCamera, body = shim)
    ScriptCameraRef worldAddCamera(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = removeCamera, body = shim)
    bool worldRemoveCamera(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = addLight, body = shim)
    ScriptLightRef worldAddLight(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = removeLight, body = shim)
    bool worldRemoveLight(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = addMesh, body = shim)
    ScriptMeshRef worldAddMesh(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = removeMesh, body = shim)
    bool worldRemoveMesh(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = addBoxShape, body = shim)
    ScriptBoxShapeRef worldAddBoxShape(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = removeBoxShape, body = shim)
    bool worldRemoveBoxShape(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = addSphereShape, body = shim)
    ScriptSphereShapeRef worldAddSphereShape(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = removeSphereShape, body = shim)
    bool worldRemoveSphereShape(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = addAnimator, body = shim)
    ScriptAnimatorRef worldAddAnimator(ScriptContext& ctx, const ScriptEntity& entity);
    VBIND_FN(module = World, name = removeAnimator, body = shim)
    bool worldRemoveAnimator(ScriptContext& ctx, const ScriptEntity& entity);

    // --- Mesh usertype ---
    VBIND_PROPERTY(usertype = Mesh, name = builtinGeometry, set = meshSetBuiltinGeometry)
    std::uint32_t meshGetBuiltinGeometry(ScriptContext& ctx, const ScriptMeshRef& self);
    void meshSetBuiltinGeometry(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t value);

    VBIND_FN(usertype = Mesh, name = setMaterial, body = shim)
    void meshSetMaterial(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot, const std::string& uri);
    VBIND_FN(usertype = Mesh, name = setMaterialFloat, body = shim)
    void meshSetMaterialFloat(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot,
                              const std::string& name, float value);
    VBIND_FN(usertype = Mesh, name = setMaterialColor, body = shim)
    void meshSetMaterialColor(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot,
                              const std::string& name, const ScriptVec4& value);
    VBIND_FN(usertype = Mesh, name = setMaterialTexture, body = shim)
    void meshSetMaterialTexture(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot,
                                const std::string& name, const std::string& uri);
    VBIND_FN(usertype = Mesh, name = clearMaterialProperty, body = shim)
    void meshClearMaterialProperty(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot,
                                   const std::string& name);
    VBIND_FN(usertype = Mesh, name = clearMaterialProperties, body = shim)
    void meshClearMaterialProperties(ScriptContext& ctx, const ScriptMeshRef& self, std::uint32_t slot);

    // --- Layer constant table ---
    VBIND_RAW(area = world)
    void worldRegisterLayerTable(sol::state& lua, ScriptContext& ctx);
} // namespace vultra
