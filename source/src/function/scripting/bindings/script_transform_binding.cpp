#include "vultra/function/scripting/bindings/script_transform_binding.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/glm.hpp>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        ScriptVec3 toScriptVec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }

        glm::vec3 toGlmVec3(const ScriptVec3& v) { return {v.x, v.y, v.z}; }

        TransformComponent& requireTransform(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto& reg = world->registry();
            auto* tr  = reg.try_get<TransformComponent>(entity);
            if (!tr)
                throw std::runtime_error("Entity has no TransformComponent");

            return *tr;
        }
    } // namespace

    void registerScriptTransformBindings(sol::state& lua, ScriptContext& ctx)
    {
        lua.new_usertype<ScriptTransformRef>("Transform",
                                             "position",
                                             VULTRA_LUA_PROPERTY(
                                                 [&ctx](const ScriptTransformRef& self) {
                                                     return toScriptVec3(requireTransform(ctx, self.entity).position);
                                                 },
                                                 [&ctx](const ScriptTransformRef& self, const ScriptVec3& value) {
                                                     auto& tr    = requireTransform(ctx, self.entity);
                                                     tr.position = toGlmVec3(value);
                                                     tr.dirty    = true;
                                                 }),
                                             "scale",
                                             VULTRA_LUA_PROPERTY(
                                                 [&ctx](const ScriptTransformRef& self) {
                                                     return toScriptVec3(requireTransform(ctx, self.entity).scale);
                                                 },
                                                 [&ctx](const ScriptTransformRef& self, const ScriptVec3& value) {
                                                     auto& tr = requireTransform(ctx, self.entity);
                                                     tr.scale = toGlmVec3(value);
                                                     tr.dirty = true;
                                                 }),
                                             "translate",
                                             [&ctx](const ScriptTransformRef& self, const ScriptVec3& delta) {
                                                 auto& tr = requireTransform(ctx, self.entity);
                                                 tr.position += toGlmVec3(delta);
                                                 tr.dirty = true;
                                             });
    }
} // namespace vultra
