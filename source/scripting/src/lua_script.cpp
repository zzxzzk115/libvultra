#include "script_module.hpp"

#include <vultra/api/scene_bridge.hpp>
#include <vultra/api/ui_bridge.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/ui/editor_gui.hpp>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace vultra
{
    namespace
    {
        class LuaScript final : public ScriptInstance
        {
        public:
            LuaScript(const std::filesystem::path& path, SceneTree& scene, ObjectId node) :
                m_NodeId(node.value)
            {
                m_Access.scene = &scene;
                m_State        = luaL_newstate();
                if (!m_State)
                {
                    throw std::runtime_error("Create Lua state");
                }
                try
                {
                    luaL_openlibs(m_State);
                    lua_newtable(m_State);
                    bind("root_id", rootId);
                    bind("child_count", childCount);
                    bind("child_id", childId);
                    bind("node_name", nodeName);
                    bind("node_translation", nodeTranslation);
                    bind("set_node_translation", setNodeTranslation);
                    lua_setglobal(m_State, "scene");
                    lua_newtable(m_State);
                    bind("text", uiText);
                    bind("button", uiButton);
                    lua_setglobal(m_State, "gui");
                    if (luaL_dostring(m_State, kLuaPrelude) != LUA_OK)
                    {
                        throw std::runtime_error(std::string("Initialize Lua script API: ") +
                                                 lua_tostring(m_State, -1));
                    }
                    const auto name = path.string();
                    if (luaL_loadfile(m_State, name.c_str()) != LUA_OK || lua_pcall(m_State, 0, 1, 0) != LUA_OK)
                    {
                        throw std::runtime_error("Load Lua module " + name + ": " + lua_tostring(m_State, -1));
                    }
                    if (!lua_istable(m_State, -1))
                    {
                        throw std::runtime_error("Lua script must return a table: " + name);
                    }
                    m_ScriptRef = luaL_ref(m_State, LUA_REGISTRYINDEX);
                    lua_getglobal(m_State, "__vultra_attach");
                    lua_rawgeti(m_State, LUA_REGISTRYINDEX, m_ScriptRef);
                    lua_pushinteger(m_State, static_cast<lua_Integer>(m_NodeId));
                    if (lua_pcall(m_State, 2, 0, 0) != LUA_OK)
                    {
                        throw std::runtime_error("Attach Lua script " + name + ": " + lua_tostring(m_State, -1));
                    }
                }
                catch (...)
                {
                    lua_close(m_State);
                    m_State = nullptr;
                    throw;
                }
            }

            ~LuaScript() override
            {
                stop();
            }

            void update(float deltaSeconds) override
            {
                ++m_Access.serial;
                m_Access.active = true;
                m_SceneFrame    = {&m_Access, m_Access.serial};
                try
                {
                    if (!m_Ready)
                    {
                        call("_ready", false);
                        m_Ready = true;
                    }
                    call("_process", true, deltaSeconds);
                }
                catch (...)
                {
                    m_Access.active = false;
                    m_SceneFrame    = {};
                    throw;
                }
                m_Access.active = false;
                m_SceneFrame    = {};
            }

            void gui(EditorGui& gui) override
            {
                if (!gui.frameActive())
                {
                    throw std::logic_error("Lua UI requires an active GUI frame");
                }
                m_UiFrame = makeUiFrame(gui);
                try
                {
                    call("_editor_gui", false, std::nullopt, true);
                }
                catch (...)
                {
                    m_UiFrame = {};
                    throw;
                }
                m_UiFrame = {};
            }

            void stop() noexcept override
            {
                if (!m_State)
                {
                    return;
                }
                m_Access.active = false;
                m_SceneFrame    = {};
                m_UiFrame       = {};
                try
                {
                    if (m_Ready)
                    {
                        call("_exit_tree", false);
                    }
                }
                catch (const std::exception& error)
                {
                    Logger::app().warn("Lua stop callback failed: {}", error.what());
                }
                lua_close(m_State);
                m_State = nullptr;
            }

        private:
            void bind(const char* name, lua_CFunction function)
            {
                lua_pushlightuserdata(m_State, this);
                lua_pushcclosure(m_State, function, 1);
                lua_setfield(m_State, -2, name);
            }

            void call(const char* name, bool required, std::optional<float> delta = {}, bool withGui = false)
            {
                lua_rawgeti(m_State, LUA_REGISTRYINDEX, m_ScriptRef);
                lua_getfield(m_State, -1, name);
                if (lua_isnil(m_State, -1) && !required)
                {
                    lua_pop(m_State, 2);
                    return;
                }
                if (!lua_isfunction(m_State, -1))
                {
                    lua_pop(m_State, 2);
                    throw std::runtime_error(std::string("Lua callback missing: ") + name);
                }
                lua_pushvalue(m_State, -2);
                int arguments = 1;
                if (delta)
                {
                    lua_pushnumber(m_State, *delta);
                    ++arguments;
                }
                if (withGui)
                {
                    lua_getglobal(m_State, "__vultra_editor_gui");
                    ++arguments;
                }
                if (lua_pcall(m_State, arguments, 0, 0) != LUA_OK)
                {
                    const char* message = lua_tostring(m_State, -1);
                    std::string error   = std::string("Lua ") + name + ": " + (message ? message : "unknown error");
                    lua_pop(m_State, 2);
                    throw std::runtime_error(error);
                }
                lua_pop(m_State, 1);
            }

            static LuaScript* self(lua_State* state)
            {
                return static_cast<LuaScript*>(lua_touserdata(state, lua_upvalueindex(1)));
            }

            static int result(lua_State* state, VultraStatus status)
            {
                if (status != VULTRA_STATUS_OK)
                {
                    return luaL_error(state, "Vultra API status %d", int(status));
                }
                return 1;
            }

            static int rootId(lua_State* state)
            {
                auto*      script = self(state);
                uint64_t   value  = 0;
                const auto status = sceneApi().root_id(script->m_SceneFrame, &value);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(value));
                }
                return result(state, status);
            }

            static int childCount(lua_State* state)
            {
                auto*      script = self(state);
                uint64_t   value  = 0;
                const auto status = sceneApi().child_count(script->m_SceneFrame,
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                           &value);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(value));
                }
                return result(state, status);
            }

            static int childId(lua_State* state)
            {
                auto*      script = self(state);
                uint64_t   value  = 0;
                const auto status = sceneApi().child_id(script->m_SceneFrame,
                                                        static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                        static_cast<uint64_t>(luaL_checkinteger(state, 2)),
                                                        &value);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(value));
                }
                return result(state, status);
            }

            static int nodeName(lua_State* state)
            {
                auto*       script = self(state);
                const char* data   = nullptr;
                uint64_t    size   = 0;
                const auto  status = sceneApi().node_name(script->m_SceneFrame,
                                                          static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                          &data,
                                                          &size);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushlstring(state, data, size);
                }
                return result(state, status);
            }

            static int nodeTranslation(lua_State* state)
            {
                auto*                  script = self(state);
                VultraSceneTranslation value {};
                const auto status = sceneApi().node_translation(script->m_SceneFrame,
                                                                static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                &value);
                if (status != VULTRA_STATUS_OK)
                {
                    return luaL_error(state, "Vultra API status %d", int(status));
                }
                lua_pushnumber(state, value.x);
                lua_pushnumber(state, value.y);
                lua_pushnumber(state, value.z);
                return 3;
            }

            static int setNodeTranslation(lua_State* state)
            {
                auto*                        script = self(state);
                const auto                   node   = static_cast<uint64_t>(luaL_checkinteger(state, 1));
                const VultraSceneTranslation value {static_cast<float>(luaL_checknumber(state, 2)),
                                                    static_cast<float>(luaL_checknumber(state, 3)),
                                                    static_cast<float>(luaL_checknumber(state, 4))};
                const auto status = sceneApi().set_node_translation(script->m_SceneFrame, node, value);
                if (status != VULTRA_STATUS_OK)
                {
                    return luaL_error(state, "Vultra API status %d", int(status));
                }
                return 0;
            }

            static int uiText(lua_State* state)
            {
                auto*       script = self(state);
                size_t      size   = 0;
                const char* data   = luaL_checklstring(state, 1, &size);
                const auto  status = uiApi().text(script->m_UiFrame, data, size);
                if (status != VULTRA_STATUS_OK)
                {
                    return luaL_error(state, "Vultra API status %d", int(status));
                }
                return 0;
            }

            static int uiButton(lua_State* state)
            {
                auto*       script  = self(state);
                size_t      size    = 0;
                const char* data    = luaL_checklstring(state, 1, &size);
                uint8_t     clicked = 0;
                const auto  status  = uiApi().button(script->m_UiFrame, data, size, &clicked);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushboolean(state, clicked != 0);
                }
                return result(state, status);
            }

            static constexpr const char* kLuaPrelude = R"lua(
local scene_api, gui_api = scene, gui
local node = {}
node.__index = node
function node:get_child(index) return setmetatable({ id = scene_api.child_id(self.id, index) }, node) end
function node:child_count() return scene_api.child_count(self.id) end
function node:name() return scene_api.node_name(self.id) end
function node:position() return scene_api.node_translation(self.id) end
function node:set_position(x, y, z) return scene_api.set_node_translation(self.id, x, y, z) end
local world = {}
function world:root() return setmetatable({ id = scene_api.root_id() }, node) end
local editor = {}
function editor:text(message) return gui_api.text(message) end
function editor:button(label) return gui_api.button(label) end
__vultra_editor_gui = editor
function __vultra_attach(script, id)
    script.node = setmetatable({ id = id }, node)
    script.scene = world
end
scene, gui = nil, nil
)lua";

            lua_State*       m_State     = nullptr;
            int              m_ScriptRef = LUA_NOREF;
            uint64_t         m_NodeId    = 0;
            bool             m_Ready     = false;
            SceneAccess      m_Access;
            VultraSceneFrame m_SceneFrame {};
            VultraUiFrame    m_UiFrame {};
        };
    } // namespace

    std::unique_ptr<ScriptInstance> loadLuaScript(const std::filesystem::path& path, SceneTree& scene, ObjectId node)
    {
        return std::make_unique<LuaScript>(path, scene, node);
    }
} // namespace vultra
