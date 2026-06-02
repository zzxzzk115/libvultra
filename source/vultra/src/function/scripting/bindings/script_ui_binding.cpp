#include "vultra/function/scripting/bindings/script_ui_binding.hpp"

#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/ui_service.hpp"
#include "vultra/function/world/components/ui_components.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/glm.hpp>

#include <stdexcept>

namespace vultra
{
    namespace
    {
        ScriptVec2 toScriptVec2(const glm::vec2& v) { return {v.x, v.y}; }

        glm::vec2 toGlmVec2(const ScriptVec2& v) { return {v.x, v.y}; }

        RectTransformComponent& requireRectTransform(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto* rect = world->registry().try_get<RectTransformComponent>(entity);
            if (!rect)
                throw std::runtime_error("Entity has no RectTransformComponent");
            return *rect;
        }

        UiButtonComponent& requireButton(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto* button = world->registry().try_get<UiButtonComponent>(entity);
            if (!button)
                throw std::runtime_error("Entity has no UiButtonComponent");
            return *button;
        }
    } // namespace

    void registerScriptUiBindings(sol::state& lua, ScriptContext& ctx)
    {
        lua.new_usertype<ScriptRectTransformRef>(
            "RectTransform",
            "anchorMin",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRectTransformRef& self) {
                    return toScriptVec2(requireRectTransform(ctx, self.entity).anchorMin);
                },
                [&ctx](const ScriptRectTransformRef& self, const ScriptVec2& value) {
                    requireRectTransform(ctx, self.entity).anchorMin = toGlmVec2(value);
                }),
            "anchorMax",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRectTransformRef& self) {
                    return toScriptVec2(requireRectTransform(ctx, self.entity).anchorMax);
                },
                [&ctx](const ScriptRectTransformRef& self, const ScriptVec2& value) {
                    requireRectTransform(ctx, self.entity).anchorMax = toGlmVec2(value);
                }),
            "pivot",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRectTransformRef& self) {
                    return toScriptVec2(requireRectTransform(ctx, self.entity).pivot);
                },
                [&ctx](const ScriptRectTransformRef& self, const ScriptVec2& value) {
                    requireRectTransform(ctx, self.entity).pivot = toGlmVec2(value);
                }),
            "anchoredPositionPx",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRectTransformRef& self) {
                    return toScriptVec2(requireRectTransform(ctx, self.entity).anchoredPositionPx);
                },
                [&ctx](const ScriptRectTransformRef& self, const ScriptVec2& value) {
                    requireRectTransform(ctx, self.entity).anchoredPositionPx = toGlmVec2(value);
                }),
            "sizeDeltaPx",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRectTransformRef& self) {
                    return toScriptVec2(requireRectTransform(ctx, self.entity).sizeDeltaPx);
                },
                [&ctx](const ScriptRectTransformRef& self, const ScriptVec2& value) {
                    requireRectTransform(ctx, self.entity).sizeDeltaPx = toGlmVec2(value);
                }),
            "scale",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRectTransformRef& self) {
                    return toScriptVec2(requireRectTransform(ctx, self.entity).scale);
                },
                [&ctx](const ScriptRectTransformRef& self, const ScriptVec2& value) {
                    requireRectTransform(ctx, self.entity).scale = toGlmVec2(value);
                }),
            "rotationDegrees",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRectTransformRef& self) {
                    return requireRectTransform(ctx, self.entity).rotationDegrees;
                },
                [&ctx](const ScriptRectTransformRef& self, float value) {
                    requireRectTransform(ctx, self.entity).rotationDegrees = value;
                }));

        lua.new_usertype<ScriptUiButtonRef>(
            "UiButton",
            "interactable",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiButtonRef& self) { return requireButton(ctx, self.entity).interactable; },
                [&ctx](const ScriptUiButtonRef& self, bool value) {
                    requireButton(ctx, self.entity).interactable = value;
                }),
            "hovered",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptUiButtonRef& self) {
                return requireButton(ctx, self.entity).hovered;
            }),
            "pressed",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptUiButtonRef& self) {
                return requireButton(ctx, self.entity).pressed;
            }),
            "clicked",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptUiButtonRef& self) {
                return ctx.uiService && ctx.uiService->buttonClicked(self.entity);
            }));

        auto ui = lua.create_table();
        ui.set_function("isPointerOverUI", [&ctx]() { return ctx.uiService && ctx.uiService->pointerOverUi(); });
        ui.set_function("hoveredEntity", [&ctx]() {
            return ctx.uiService ? ScriptEntity {ctx.uiService->hoveredEntity()} : ScriptEntity {};
        });
        ui.set_function("pressedEntity", [&ctx]() {
            return ctx.uiService ? ScriptEntity {ctx.uiService->pressedEntity()} : ScriptEntity {};
        });
        lua["UI"] = ui;
    }
} // namespace vultra
