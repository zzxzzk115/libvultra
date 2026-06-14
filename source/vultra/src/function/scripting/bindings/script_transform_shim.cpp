#include "vultra/function/scripting/bindings/script_transform_shim.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/components/ui_components.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/geometric.hpp>

#include <stdexcept>

namespace vultra
{
    namespace
    {
        ScriptVec3 toScriptVec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }
        glm::vec3  toGlmVec3(const ScriptVec3& v) { return {v.x, v.y, v.z}; }

        ScriptVec3 eulerDegrees(const glm::quat& q)
        {
            const glm::vec3 radians = glm::eulerAngles(q);
            return {glm::degrees(radians.x), glm::degrees(radians.y), glm::degrees(radians.z)};
        }
        glm::quat quatFromEulerDegrees(const ScriptVec3& degrees) { return glm::quat(glm::radians(toGlmVec3(degrees))); }

        TransformComponent& requireTransform(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");
            auto* tr = world->registry().try_get<TransformComponent>(entity);
            if (!tr)
                throw std::runtime_error("Entity has no TransformComponent");
            return *tr;
        }

        RectTransformComponent* rectTransform(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            return world ? world->registry().try_get<RectTransformComponent>(entity) : nullptr;
        }

        void setRotationDegrees(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value)
        {
            if (auto* rect = rectTransform(ctx, self.entity))
            {
                rect->rotation = value.z;
                return;
            }
            auto& tr    = requireTransform(ctx, self.entity);
            tr.rotation = quatFromEulerDegrees(value);
            tr.dirty    = true;
        }
        ScriptVec3 getRotationDegrees(ScriptContext& ctx, const ScriptTransformRef& self)
        {
            if (auto* rect = rectTransform(ctx, self.entity))
                return toScriptVec3(glm::vec3(0.0f, 0.0f, rect->rotation));
            return eulerDegrees(requireTransform(ctx, self.entity).rotation);
        }
    } // namespace

    ScriptVec3 transformGetPosition(ScriptContext& ctx, const ScriptTransformRef& self)
    {
        if (auto* rect = rectTransform(ctx, self.entity))
            return toScriptVec3(glm::vec3(rect->anchoredPositionPx.x, rect->anchoredPositionPx.y, 0.0f));
        return toScriptVec3(requireTransform(ctx, self.entity).position);
    }
    void transformSetPosition(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value)
    {
        if (auto* rect = rectTransform(ctx, self.entity))
        {
            rect->anchoredPositionPx = glm::vec2(value.x, value.y);
            return;
        }
        auto& tr    = requireTransform(ctx, self.entity);
        tr.position = toGlmVec3(value);
        tr.dirty    = true;
    }

    ScriptVec3 transformGetScale(ScriptContext& ctx, const ScriptTransformRef& self)
    {
        if (auto* rect = rectTransform(ctx, self.entity))
            return toScriptVec3(glm::vec3(rect->scale.x, rect->scale.y, 1.0f));
        return toScriptVec3(requireTransform(ctx, self.entity).scale);
    }
    void transformSetScale(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value)
    {
        if (auto* rect = rectTransform(ctx, self.entity))
        {
            rect->scale = glm::vec2(value.x, value.y);
            return;
        }
        auto& tr = requireTransform(ctx, self.entity);
        tr.scale = toGlmVec3(value);
        tr.dirty = true;
    }

    ScriptVec3 transformGetRotation(ScriptContext& ctx, const ScriptTransformRef& self)
    {
        return getRotationDegrees(ctx, self);
    }
    void transformSetRotation(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value)
    {
        setRotationDegrees(ctx, self, value);
    }

    ScriptVec3 transformGetRotationEuler(ScriptContext& ctx, const ScriptTransformRef& self)
    {
        script_binding::warnDeprecated("Transform.rotationEuler", "Transform.rotation");
        return getRotationDegrees(ctx, self);
    }
    void transformSetRotationEuler(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value)
    {
        script_binding::warnDeprecated("Transform.rotationEuler", "Transform.rotation");
        setRotationDegrees(ctx, self, value);
    }

    void transformTranslate(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& delta)
    {
        if (auto* rect = rectTransform(ctx, self.entity))
        {
            rect->anchoredPositionPx += glm::vec2(delta.x, delta.y);
            return;
        }
        auto& tr = requireTransform(ctx, self.entity);
        tr.position += toGlmVec3(delta);
        tr.dirty = true;
    }

    void transformSetEulerDegrees(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& value)
    {
        script_binding::warnDeprecated("Transform.setEulerDegrees", "Transform.rotation");
        setRotationDegrees(ctx, self, value);
    }

    void transformLookAt(ScriptContext& ctx, const ScriptTransformRef& self, const ScriptVec3& target)
    {
        if (rectTransform(ctx, self.entity))
            return;
        auto&           tr        = requireTransform(ctx, self.entity);
        const glm::vec3 direction = toGlmVec3(target) - tr.position;
        if (glm::length2(direction) <= 0.000001f)
            return;
        tr.rotation = glm::quatLookAtRH(glm::normalize(direction), glm::vec3 {0.0f, 1.0f, 0.0f});
        tr.dirty    = true;
    }
} // namespace vultra
