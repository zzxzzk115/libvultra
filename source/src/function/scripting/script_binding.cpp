#include "vultra/function/scripting/script_binding.hpp"

#include "vultra/core/services/input_service.hpp"
#include "vultra/function/scripting/script_types.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

#include <glm/glm.hpp>
#include <sol/property.hpp>

namespace vultra
{
    namespace
    {
        ScriptVec2 toScriptVec2(const glm::vec2& v) { return {v.x, v.y}; }

        ScriptVec3 toScriptVec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }

        glm::vec3 toGlmVec3(const ScriptVec3& v) { return {v.x, v.y, v.z}; }
    } // namespace

    void registerScriptBindings(sol::state& lua, ScriptContext& ctx)
    {
        lua.new_usertype<ScriptVec2>(
            "Vec2",
            sol::constructors<ScriptVec2(), ScriptVec2(float, float)>(),
            "x",
            sol::property([](const ScriptVec2& v) { return v.x; }, [](ScriptVec2& v, float value) { v.x = value; }),
            "y",
            sol::property([](const ScriptVec2& v) { return v.y; }, [](ScriptVec2& v, float value) { v.y = value; }));

        lua.new_usertype<ScriptVec3>(
            "Vec3",
            sol::constructors<ScriptVec3(), ScriptVec3(float, float, float)>(),
            "x",
            sol::property([](const ScriptVec3& v) { return v.x; }, [](ScriptVec3& v, float value) { v.x = value; }),
            "y",
            sol::property([](const ScriptVec3& v) { return v.y; }, [](ScriptVec3& v, float value) { v.y = value; }),
            "z",
            sol::property([](const ScriptVec3& v) { return v.z; }, [](ScriptVec3& v, float value) { v.z = value; }));

        lua.new_usertype<ScriptEntity>(
            "Entity",
            "valid",
            [&ctx](const ScriptEntity& self) { return ctx.isValid(self.value); },
            "get_position",
            [&ctx](const ScriptEntity& self) -> ScriptVec3 {
                auto* world = ctx.world();
                if (!world)
                    return {};

                auto& reg = world->registry();
                auto* tr  = reg.try_get<TransformComponent>(self.value);
                return tr ? toScriptVec3(tr->position) : ScriptVec3 {};
            },
            "set_position",
            [&ctx](const ScriptEntity& self, const ScriptVec3& v) {
                auto* world = ctx.world();
                if (!world)
                    return;

                auto& reg = world->registry();
                auto* tr  = reg.try_get<TransformComponent>(self.value);
                if (!tr)
                    return;

                tr->position = toGlmVec3(v);
                tr->dirty    = true;
            },
            "translate",
            [&ctx](const ScriptEntity& self, const ScriptVec3& delta) {
                auto* world = ctx.world();
                if (!world)
                    return;

                auto& reg = world->registry();
                auto* tr  = reg.try_get<TransformComponent>(self.value);
                if (!tr)
                    return;

                tr->position += toGlmVec3(delta);
                tr->dirty = true;
            });

        lua["Input"] = lua.create_table();

        lua["Input"]["getKey"] = [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->getKey(static_cast<KeyCode>(key)) : false;
        };

        lua["Input"]["getKeyDown"] = [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->getKeyDown(static_cast<KeyCode>(key)) : false;
        };

        lua["Input"]["getKeyUp"] = [&ctx](int key) {
            return ctx.inputService ? ctx.inputService->getKeyUp(static_cast<KeyCode>(key)) : false;
        };

        lua["Input"]["getMousePosition"] = [&ctx]() {
            return ctx.inputService ? toScriptVec2(ctx.inputService->getMousePosition()) : ScriptVec2 {};
        };

        lua["KeyCode"] = lua.create_table_with("W",
                                               static_cast<int>(KeyCode::eW),
                                               "A",
                                               static_cast<int>(KeyCode::eA),
                                               "S",
                                               static_cast<int>(KeyCode::eS),
                                               "D",
                                               static_cast<int>(KeyCode::eD),
                                               "Space",
                                               static_cast<int>(KeyCode::eSpace),
                                               "Left",
                                               static_cast<int>(KeyCode::eLeft),
                                               "Right",
                                               static_cast<int>(KeyCode::eRight),
                                               "Up",
                                               static_cast<int>(KeyCode::eUp),
                                               "Down",
                                               static_cast<int>(KeyCode::eDown));
    }
} // namespace vultra