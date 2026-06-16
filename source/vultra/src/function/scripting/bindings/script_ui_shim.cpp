#include "vultra/function/scripting/bindings/script_ui_shim.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/services/input_service.hpp"
#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/services/ui_service.hpp"
#include "vultra/function/world/components/ui_components.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace vultra
{
    namespace
    {
        struct ScriptUiSignalRef
        {
            entt::entity entity {entt::null};
            std::string  name;
        };

        struct ScriptUiSignalConnection
        {
            uint64_t id {0};
        };

        struct ScriptUiEvent
        {
            std::string type;
            ScriptEntity target;
            ScriptEntity currentTarget;
            ScriptEntity canvas;
            ScriptVec2 screenPosition;
            ScriptVec2 canvasPosition;
            ScriptVec2 localPosition;
            uint32_t button {0};
            uint32_t clickCount {0};
            std::shared_ptr<bool> handled {std::make_shared<bool>(false)};

            void stopPropagation()
            {
                if (handled)
                    *handled = true;
            }

            bool isHandled() const { return handled && *handled; }
        };

        struct ScriptUiRaycastHit
        {
            bool hit {false};
            ScriptEntity entity;
            ScriptEntity canvas;
            ScriptVec2 screenPosition;
            ScriptVec2 canvasPosition;
            ScriptVec2 localPosition;
            int sortOrder {0};
            uint32_t depth {0};
        };

        struct SignalConnectionRecord
        {
            uint64_t id {0};
            entt::entity entity {entt::null};
            std::string signal;
            sol::protected_function callback;
            bool active {true};
        };

        std::vector<SignalConnectionRecord>& signalConnections()
        {
            static std::vector<SignalConnectionRecord> connections;
            return connections;
        }

        uint64_t& nextConnectionId()
        {
            static uint64_t next = 1;
            return next;
        }

        ScriptVec2 toScriptVec2(const glm::vec2& v) { return {v.x, v.y}; }

        glm::vec2 toGlmVec2(const ScriptVec2& v) { return {v.x, v.y}; }

        std::string eventTypeName(UiEventType type)
        {
            switch (type)
            {
                case UiEventType::PointerEnter:
                    return "PointerEnter";
                case UiEventType::PointerExit:
                    return "PointerExit";
                case UiEventType::PointerMove:
                    return "PointerMove";
                case UiEventType::PointerDown:
                    return "PointerDown";
                case UiEventType::PointerUp:
                    return "PointerUp";
                case UiEventType::Click:
                    return "Click";
                case UiEventType::ValueChanged:
                    return "ValueChanged";
                case UiEventType::Submit:
                    return "Submit";
            }
            return "PointerMove";
        }

        bool signalMatchesEvent(const std::string& signal, UiEventType type)
        {
            if (signal == "onPointerEnter")
                return type == UiEventType::PointerEnter;
            if (signal == "onPointerExit")
                return type == UiEventType::PointerExit;
            if (signal == "onPointerMove")
                return type == UiEventType::PointerMove;
            if (signal == "onPointerDown")
                return type == UiEventType::PointerDown;
            if (signal == "onPointerUp")
                return type == UiEventType::PointerUp;
            if (signal == "onClick" || signal == "clicked")
                return type == UiEventType::Click;
            if (signal == "onValueChanged")
                return type == UiEventType::ValueChanged;
            if (signal == "onSubmit")
                return type == UiEventType::Submit;
            return false;
        }

        ScriptUiEvent toScriptEvent(const UiPointerEvent& event)
        {
            return ScriptUiEvent {
                .type = eventTypeName(event.type),
                .target = ScriptEntity {event.target},
                .currentTarget = ScriptEntity {event.currentTarget},
                .canvas = ScriptEntity {event.canvas},
                .screenPosition = toScriptVec2(event.screenPosition),
                .canvasPosition = toScriptVec2(event.canvasPosition),
                .localPosition = toScriptVec2(event.localPosition),
                .button = event.button,
                .clickCount = event.clickCount,
            };
        }

        ScriptUiRaycastHit toScriptHit(const std::optional<UiRaycastHit>& hit)
        {
            if (!hit)
                return {};
            return ScriptUiRaycastHit {
                .hit = true,
                .entity = ScriptEntity {hit->entity},
                .canvas = ScriptEntity {hit->canvas},
                .screenPosition = toScriptVec2(hit->screenPx),
                .canvasPosition = toScriptVec2(hit->canvasPx),
                .localPosition = toScriptVec2(hit->localPx),
                .sortOrder = hit->sortOrder,
                .depth = hit->depth,
            };
        }

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

        UiToggleComponent& requireToggle(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto* toggle = world->registry().try_get<UiToggleComponent>(entity);
            if (!toggle)
                throw std::runtime_error("Entity has no UiToggleComponent");
            return *toggle;
        }

        UiSliderComponent& requireSlider(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto* slider = world->registry().try_get<UiSliderComponent>(entity);
            if (!slider)
                throw std::runtime_error("Entity has no UiSliderComponent");
            return *slider;
        }

        UiProgressBarComponent& requireProgressBar(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto* progress = world->registry().try_get<UiProgressBarComponent>(entity);
            if (!progress)
                throw std::runtime_error("Entity has no UiProgressBarComponent");
            return *progress;
        }

        UiInputFieldComponent& requireInputField(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto* field = world->registry().try_get<UiInputFieldComponent>(entity);
            if (!field)
                throw std::runtime_error("Entity has no UiInputFieldComponent");
            return *field;
        }

        UiDropdownComponent& requireDropdown(ScriptContext& ctx, entt::entity entity)
        {
            auto* world = ctx.world();
            if (!world)
                throw std::runtime_error("ScriptContext has no World");

            auto* dropdown = world->registry().try_get<UiDropdownComponent>(entity);
            if (!dropdown)
                throw std::runtime_error("Entity has no UiDropdownComponent");
            return *dropdown;
        }

        ScriptUiSignalConnection connectSignal(const ScriptUiSignalRef& signal, sol::protected_function callback)
        {
            if (!callback.valid())
                return {};
            const uint64_t id = nextConnectionId()++;
            signalConnections().push_back(SignalConnectionRecord {
                .id = id,
                .entity = signal.entity,
                .signal = signal.name,
                .callback = std::move(callback),
                .active = true,
            });
            return ScriptUiSignalConnection {id};
        }

        void disconnectSignal(const ScriptUiSignalConnection& connection)
        {
            for (auto& record : signalConnections())
            {
                if (record.id == connection.id)
                {
                    record.active = false;
                    return;
                }
            }
        }

        void compactSignalConnections()
        {
            auto& connections = signalConnections();
            connections.erase(std::remove_if(connections.begin(),
                                             connections.end(),
                                             [](const SignalConnectionRecord& record) { return !record.active; }),
                              connections.end());
        }
    } // namespace

    void uiRegisterBindings(sol::state& lua, ScriptContext& ctx)
    {
        lua.new_usertype<ScriptUiEvent>(
            "UiEvent",
            "type",
            sol::readonly(&ScriptUiEvent::type),
            "target",
            sol::readonly(&ScriptUiEvent::target),
            "currentTarget",
            sol::readonly(&ScriptUiEvent::currentTarget),
            "canvas",
            sol::readonly(&ScriptUiEvent::canvas),
            "screenPosition",
            sol::readonly(&ScriptUiEvent::screenPosition),
            "canvasPosition",
            sol::readonly(&ScriptUiEvent::canvasPosition),
            "localPosition",
            sol::readonly(&ScriptUiEvent::localPosition),
            "button",
            sol::readonly(&ScriptUiEvent::button),
            "clickCount",
            sol::readonly(&ScriptUiEvent::clickCount),
            "handled",
            VULTRA_LUA_READONLY_PROPERTY([](const ScriptUiEvent& self) { return self.isHandled(); }),
            "stopPropagation",
            &ScriptUiEvent::stopPropagation);

        lua.new_usertype<ScriptUiRaycastHit>(
            "UiRaycastHit",
            "hit",
            sol::readonly(&ScriptUiRaycastHit::hit),
            "entity",
            sol::readonly(&ScriptUiRaycastHit::entity),
            "canvas",
            sol::readonly(&ScriptUiRaycastHit::canvas),
            "screenPosition",
            sol::readonly(&ScriptUiRaycastHit::screenPosition),
            "canvasPosition",
            sol::readonly(&ScriptUiRaycastHit::canvasPosition),
            "localPosition",
            sol::readonly(&ScriptUiRaycastHit::localPosition),
            "sortOrder",
            sol::readonly(&ScriptUiRaycastHit::sortOrder),
            "depth",
            sol::readonly(&ScriptUiRaycastHit::depth));

        lua.new_usertype<ScriptUiSignalConnection>(
            "UiSignalConnection",
            "disconnect",
            [](const ScriptUiSignalConnection& self) { disconnectSignal(self); });

        lua.new_usertype<ScriptUiSignalRef>(
            "UiSignal",
            "connect",
            [](const ScriptUiSignalRef& self, sol::protected_function callback) {
                return connectSignal(self, std::move(callback));
            });

        lua.new_usertype<ScriptUiRef>(
            "Ui",
            "onPointerEnter",
            sol::property([](const ScriptUiRef& self) {
                return ScriptUiSignalRef {self.entity, "onPointerEnter"};
            }),
            "onPointerExit",
            sol::property([](const ScriptUiRef& self) {
                return ScriptUiSignalRef {self.entity, "onPointerExit"};
            }),
            "onPointerMove",
            sol::property([](const ScriptUiRef& self) {
                return ScriptUiSignalRef {self.entity, "onPointerMove"};
            }),
            "onPointerDown",
            sol::property([](const ScriptUiRef& self) {
                return ScriptUiSignalRef {self.entity, "onPointerDown"};
            }),
            "onPointerUp",
            sol::property([](const ScriptUiRef& self) {
                return ScriptUiSignalRef {self.entity, "onPointerUp"};
            }),
            "onClick",
            sol::property([](const ScriptUiRef& self) {
                return ScriptUiSignalRef {self.entity, "onClick"};
            }));

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
            "rotation",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRectTransformRef& self) {
                    return requireRectTransform(ctx, self.entity).rotation;
                },
                [&ctx](const ScriptRectTransformRef& self, float value) {
                    requireRectTransform(ctx, self.entity).rotation = value;
                }),
            "rotationDegrees",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptRectTransformRef& self) {
                    script_binding::warnDeprecated("RectTransform.rotationDegrees", "RectTransform.rotation");
                    return requireRectTransform(ctx, self.entity).rotation;
                },
                [&ctx](const ScriptRectTransformRef& self, float value) {
                    script_binding::warnDeprecated("RectTransform.rotationDegrees", "RectTransform.rotation");
                    requireRectTransform(ctx, self.entity).rotation = value;
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
            "onClick",
            sol::property([](const ScriptUiButtonRef& self) {
                return ScriptUiSignalRef {self.entity, "onClick"};
            }),
            "clicked",
            sol::property([](const ScriptUiButtonRef& self) {
                script_binding::warnDeprecated("UiButton.clicked", "UiButton.onClick");
                return ScriptUiSignalRef {self.entity, "clicked"};
            }),
            "clickedThisFrame",
            VULTRA_LUA_READONLY_PROPERTY([&ctx](const ScriptUiButtonRef& self) {
                return ctx.uiService && ctx.uiService->buttonClicked(self.entity);
            }));

        lua.new_usertype<ScriptUiToggleRef>(
            "UiToggle",
            "interactable",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiToggleRef& self) { return requireToggle(ctx, self.entity).interactable; },
                [&ctx](const ScriptUiToggleRef& self, bool value) {
                    requireToggle(ctx, self.entity).interactable = value;
                }),
            "checked",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiToggleRef& self) { return requireToggle(ctx, self.entity).checked; },
                [&ctx](const ScriptUiToggleRef& self, bool value) {
                    requireToggle(ctx, self.entity).checked = value;
                }),
            "onClick",
            sol::property([](const ScriptUiToggleRef& self) {
                return ScriptUiSignalRef {self.entity, "onClick"};
            }),
            "clicked",
            sol::property([](const ScriptUiToggleRef& self) {
                script_binding::warnDeprecated("UiToggle.clicked", "UiToggle.onClick");
                return ScriptUiSignalRef {self.entity, "clicked"};
            }));

        lua.new_usertype<ScriptUiSliderRef>(
            "UiSlider",
            "interactable",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiSliderRef& self) { return requireSlider(ctx, self.entity).interactable; },
                [&ctx](const ScriptUiSliderRef& self, bool value) {
                    requireSlider(ctx, self.entity).interactable = value;
                }),
            "value",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiSliderRef& self) { return requireSlider(ctx, self.entity).value; },
                [&ctx](const ScriptUiSliderRef& self, float value) {
                    auto& slider = requireSlider(ctx, self.entity);
                    slider.value = std::clamp(value, slider.minValue, slider.maxValue);
                }),
            "minValue",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiSliderRef& self) { return requireSlider(ctx, self.entity).minValue; },
                [&ctx](const ScriptUiSliderRef& self, float value) {
                    requireSlider(ctx, self.entity).minValue = value;
                }),
            "maxValue",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiSliderRef& self) { return requireSlider(ctx, self.entity).maxValue; },
                [&ctx](const ScriptUiSliderRef& self, float value) {
                    requireSlider(ctx, self.entity).maxValue = value;
                }));

        lua.new_usertype<ScriptUiProgressBarRef>(
            "UiProgressBar",
            "value",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiProgressBarRef& self) { return requireProgressBar(ctx, self.entity).value; },
                [&ctx](const ScriptUiProgressBarRef& self, float value) {
                    auto& progress = requireProgressBar(ctx, self.entity);
                    progress.value = std::clamp(value, progress.minValue, progress.maxValue);
                }),
            "minValue",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiProgressBarRef& self) { return requireProgressBar(ctx, self.entity).minValue; },
                [&ctx](const ScriptUiProgressBarRef& self, float value) {
                    requireProgressBar(ctx, self.entity).minValue = value;
                }),
            "maxValue",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiProgressBarRef& self) { return requireProgressBar(ctx, self.entity).maxValue; },
                [&ctx](const ScriptUiProgressBarRef& self, float value) {
                    requireProgressBar(ctx, self.entity).maxValue = value;
                }));

        lua.new_usertype<ScriptUiInputFieldRef>(
            "UiInputField",
            "text",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiInputFieldRef& self) { return requireInputField(ctx, self.entity).text; },
                [&ctx](const ScriptUiInputFieldRef& self, std::string value) {
                    auto& field = requireInputField(ctx, self.entity);
                    field.text  = std::move(value);
                    field.caret = static_cast<int>(field.text.size());
                }),
            "placeholder",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiInputFieldRef& self) { return requireInputField(ctx, self.entity).placeholder; },
                [&ctx](const ScriptUiInputFieldRef& self, std::string value) {
                    requireInputField(ctx, self.entity).placeholder = std::move(value);
                }),
            "interactable",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiInputFieldRef& self) { return requireInputField(ctx, self.entity).interactable; },
                [&ctx](const ScriptUiInputFieldRef& self, bool value) {
                    requireInputField(ctx, self.entity).interactable = value;
                }),
            "focused",
            VULTRA_LUA_READONLY_PROPERTY(
                [&ctx](const ScriptUiInputFieldRef& self) { return requireInputField(ctx, self.entity).focused; }),
            "submitted",
            VULTRA_LUA_READONLY_PROPERTY(
                [&ctx](const ScriptUiInputFieldRef& self) { return requireInputField(ctx, self.entity).submitted; }),
            "onValueChanged",
            sol::property([](const ScriptUiInputFieldRef& self) {
                return ScriptUiSignalRef {self.entity, "onValueChanged"};
            }),
            "onSubmit",
            sol::property([](const ScriptUiInputFieldRef& self) {
                return ScriptUiSignalRef {self.entity, "onSubmit"};
            }));

        lua.new_usertype<ScriptUiDropdownRef>(
            "UiDropdown",
            "selectedIndex",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiDropdownRef& self) { return requireDropdown(ctx, self.entity).selectedIndex; },
                [&ctx](const ScriptUiDropdownRef& self, int value) {
                    auto& dropdown = requireDropdown(ctx, self.entity);
                    const int count = static_cast<int>(dropdown.options.size());
                    dropdown.selectedIndex = count > 0 ? std::clamp(value, 0, count - 1) : 0;
                }),
            "options",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiDropdownRef& self) { return requireDropdown(ctx, self.entity).options; },
                [&ctx](const ScriptUiDropdownRef& self, sol::table value) {
                    auto& dropdown = requireDropdown(ctx, self.entity);
                    dropdown.options.clear();
                    for (std::size_t i = 1; i <= value.size(); ++i)
                        dropdown.options.push_back(value.get<std::string>(i));
                    const int count = static_cast<int>(dropdown.options.size());
                    dropdown.selectedIndex = count > 0 ? std::clamp(dropdown.selectedIndex, 0, count - 1) : 0;
                }),
            "interactable",
            VULTRA_LUA_PROPERTY(
                [&ctx](const ScriptUiDropdownRef& self) { return requireDropdown(ctx, self.entity).interactable; },
                [&ctx](const ScriptUiDropdownRef& self, bool value) {
                    requireDropdown(ctx, self.entity).interactable = value;
                }),
            "expanded",
            VULTRA_LUA_READONLY_PROPERTY(
                [&ctx](const ScriptUiDropdownRef& self) { return requireDropdown(ctx, self.entity).expanded; }),
            "onValueChanged",
            sol::property([](const ScriptUiDropdownRef& self) {
                return ScriptUiSignalRef {self.entity, "onValueChanged"};
            }));

        auto ui = lua.create_table();
        ui.set_function("isPointerOverUI", [&ctx]() { return ctx.uiService && ctx.uiService->pointerOverUi(); });
        ui.set_function("hoveredEntity", [&ctx]() {
            return ctx.uiService ? ScriptEntity {ctx.uiService->hoveredEntity()} : ScriptEntity {};
        });
        ui.set_function("pressedEntity", [&ctx]() {
            return ctx.uiService ? ScriptEntity {ctx.uiService->pressedEntity()} : ScriptEntity {};
        });
        ui.set_function("raycast", [&ctx](sol::optional<ScriptVec2> screenPx) {
            if (!ctx.uiService)
                return ScriptUiRaycastHit {};
            if (screenPx)
                return toScriptHit(ctx.uiService->raycast(toGlmVec2(*screenPx)));
            if (!ctx.inputService)
                return ScriptUiRaycastHit {};
            return toScriptHit(ctx.uiService->raycast(ctx.inputService->mousePosition()));
        });
        ui.set_function("events", [&ctx](sol::this_state luaState) {
            sol::state_view luaView(luaState);
            sol::table out = luaView.create_table();
            if (!ctx.uiService)
                return out;
            int index = 1;
            for (const auto& event : ctx.uiService->eventsThisFrame())
                out[index++] = toScriptEvent(event);
            return out;
        });
        lua["UI"] = ui;
    }

    void dispatchScriptUiSignals(sol::state&, ScriptContext& ctx)
    {
        if (!ctx.uiService)
            return;

        std::unordered_set<uint64_t> stoppedSequences;
        for (const auto& event : ctx.uiService->eventsThisFrame())
        {
            if (stoppedSequences.contains(event.sequence))
                continue;

            for (auto& connection : signalConnections())
            {
                if (!connection.active || connection.entity != event.currentTarget ||
                    !signalMatchesEvent(connection.signal, event.type))
                    continue;

                ScriptUiEvent scriptEvent = toScriptEvent(event);
                sol::protected_function_result result = connection.callback(scriptEvent);
                if (!result.valid())
                {
                    sol::error err = result;
                    VULTRA_CORE_ERROR("[ScriptSystem] UI signal callback error: {}", err.what());
                }
                if (scriptEvent.isHandled())
                {
                    stoppedSequences.insert(event.sequence);
                    break;
                }
            }
        }
        compactSignalConnections();
    }

    void clearScriptUiSignalConnections()
    {
        signalConnections().clear();
    }

    void clearScriptUiSignalConnections(entt::entity entity)
    {
        for (auto& connection : signalConnections())
        {
            if (connection.entity == entity)
                connection.active = false;
        }
        compactSignalConnections();
    }
} // namespace vultra
