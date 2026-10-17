#include "script_module.hpp"

#include <vultra/api/scene_bridge.hpp>
#ifdef VULTRA_SCRIPT_GUI
#include <vultra/api/ui_bridge.hpp>
#include <vultra/ui/editor_gui.hpp>
#endif
#include <vultra/core/base/logger.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/lua_values.generated.hpp>

#include <nlohmann/json.hpp>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cmath>
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
            LuaScript(const std::filesystem::path& path,
                      SceneTree&                   scene,
                      ObjectId                     node,
                      const ProjectManifest*       project) :
                m_NodeId(node.value)
            {
                m_Access.scene   = &scene;
                m_Access.project = project;
                m_State          = luaL_newstate();
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
                    bind("get_property", getProperty);
                    bind("set_property", setProperty);
                    lua_pushlightuserdata(m_State, nullptr);
                    lua_setfield(m_State, -2, "null");
                    bind("node_type", nodeType);
                    bind("node_parent_id", nodeParentId);
                    bind("node_owner_id", nodeOwnerId);
                    bind("set_node_owner", setNodeOwner);
                    bind("node_execution", nodeExecution);
                    bind("set_node_process_mode", setNodeProcessMode);
                    bind("set_node_process_priority", setNodeProcessPriority);
                    bind("queue_free", queueFree);
                    bind("execution", execution);
                    bind("action", actionState);
                    bind("set_paused", setPaused);
                    bind("set_time_scale", setTimeScale);
                    bind("node_translation", nodeTranslation);
                    bind("set_node_translation", setNodeTranslation);
                    bind("create_node", createNode);
                    bind("create_node3d", createNode3D);
                    bind("create_mesh", createMesh);
                    bind("reparent_node", reparentNode);
                    bind("duplicate_mesh", duplicateMesh);
                    bind("copy_mesh_model", copyMeshModel);
                    bind("set_mesh_model", setMeshModel);
                    bind("remove_node", removeNode);
                    bind("create_camera", createCamera);
                    bind("camera_settings", cameraSettings);
                    bind("set_camera_settings", setCameraSettings);
                    bind("set_current_camera", setCurrentCamera);
                    bind("create_light", createLight);
                    bind("create_environment", createEnvironment);
                    bind("environment_settings", environmentSettings);
                    bind("set_environment_settings", setEnvironmentSettings);
                    bind("set_environment_asset", setEnvironmentAsset);
                    bind("set_current_environment", setCurrentEnvironment);
                    bind("light_kind", lightKind);
                    bind("light_settings", lightSettings);
                    bind("set_light_settings", setLightSettings);
                    bind("object_type", objectType);
                    bind("create_resource", createResource);
                    bind("find_resource", findResource);
                    bind("remove_resource", removeResource);
                    bind("create_material", createMaterial);
                    bind("material_name", materialName);
                    bind("material_parameters", materialParameters);
                    bind("set_material_parameters", setMaterialParameters);
                    bind("mesh_material", meshMaterial);
                    bind("set_mesh_material", setMeshMaterial);
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
                    call("_process", false, deltaSeconds);
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
#ifdef VULTRA_SCRIPT_GUI
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
#else
                throw std::logic_error("Lua UI requires the api_ui feature");
#endif
            }

            void notify(VultraSceneEvent event, float deltaSeconds) override
            {
                ++m_Access.serial;
                m_Access.active = true;
                m_SceneFrame    = {&m_Access, m_Access.serial};
                try
                {
                    switch (event)
                    {
                        case VULTRA_SCENE_ENTER_TREE:
                            call("_enter_tree", false);
                            break;
                        case VULTRA_SCENE_READY:
                            call("_ready", false);
                            break;
                        case VULTRA_SCENE_PHYSICS_PROCESS:
                            call("_physics_process", false, deltaSeconds);
                            break;
                        case VULTRA_SCENE_EXIT_TREE:
                            call("_exit_tree", false);
                            break;
                        case VULTRA_SCENE_RELOADED:
                            break;
                        default:
                            throw std::invalid_argument("Unknown Lua scene event");
                    }
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

            void stop() noexcept override
            {
                if (!m_State)
                {
                    return;
                }
                m_Access.active = false;
                m_SceneFrame    = {};
                m_UiFrame       = {};
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

            static int result(lua_State* state, VultraStatus status, int valueCount = 1)
            {
                if (status != VULTRA_STATUS_OK)
                {
                    return luaL_error(state, "Vultra API status %d", int(status));
                }
                return valueCount;
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

            static int nodeExecution(lua_State* state)
            {
                const auto frame    = self(state)->m_SceneFrame;
                const auto id       = static_cast<uint64_t>(luaL_checkinteger(state, 1));
                uint64_t   mode     = 0;
                int32_t    priority = 0;
                uint64_t   inside   = 0;
                uint64_t   queued   = 0;
                auto       status   = sceneApi().node_process_mode(frame, id, &mode);
                if (status == VULTRA_STATUS_OK)
                {
                    status = sceneApi().node_process_priority(frame, id, &priority);
                }
                if (status == VULTRA_STATUS_OK)
                {
                    status = sceneApi().node_inside_tree(frame, id, &inside);
                }
                if (status == VULTRA_STATUS_OK)
                {
                    status = sceneApi().node_queued_for_deletion(frame, id, &queued);
                }
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(mode));
                    lua_pushinteger(state, priority);
                    lua_pushboolean(state, inside != 0);
                    lua_pushboolean(state, queued != 0);
                }
                return result(state, status, 4);
            }

            static int setNodeProcessMode(lua_State* state)
            {
                return result(state,
                              sceneApi().set_node_process_mode(self(state)->m_SceneFrame,
                                                               static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                               static_cast<uint64_t>(luaL_checkinteger(state, 2))),
                              0);
            }

            static int setNodeProcessPriority(lua_State* state)
            {
                const auto priority = luaL_checkinteger(state, 2);
                if (priority < INT32_MIN || priority > INT32_MAX)
                {
                    return luaL_error(state, "Process priority must fit int32");
                }
                return result(state,
                              sceneApi().set_node_process_priority(self(state)->m_SceneFrame,
                                                                   static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                   static_cast<int32_t>(priority)),
                              0);
            }

            static int queueFree(lua_State* state)
            {
                return result(state,
                              sceneApi().queue_free(self(state)->m_SceneFrame,
                                                    static_cast<uint64_t>(luaL_checkinteger(state, 1))),
                              0);
            }

            static int execution(lua_State* state)
            {
                const auto frame  = self(state)->m_SceneFrame;
                uint64_t   paused = 0;
                uint64_t   tick   = 0;
                double     scale  = 0;
                auto       status = sceneApi().paused(frame, &paused);
                if (status == VULTRA_STATUS_OK)
                {
                    status = sceneApi().physics_frame(frame, &tick);
                }
                if (status == VULTRA_STATUS_OK)
                {
                    status = sceneApi().time_scale(frame, &scale);
                }
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushboolean(state, paused != 0);
                    lua_pushinteger(state, static_cast<lua_Integer>(tick));
                    lua_pushnumber(state, scale);
                }
                return result(state, status, 3);
            }

            static int setPaused(lua_State* state)
            {
                luaL_checktype(state, 1, LUA_TBOOLEAN);
                return result(state, sceneApi().set_paused(self(state)->m_SceneFrame, lua_toboolean(state, 1)), 0);
            }

            static int setTimeScale(lua_State* state)
            {
                return result(state,
                              sceneApi().set_time_scale(self(state)->m_SceneFrame, luaL_checknumber(state, 1)),
                              0);
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

            static int nodeParentId(lua_State* state)
            {
                auto*      script = self(state);
                uint64_t   value  = 0;
                const auto status = sceneApi().node_parent_id(script->m_SceneFrame,
                                                              static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                              &value);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(value));
                }
                return result(state, status);
            }

            static int nodeOwnerId(lua_State* state)
            {
                auto*      script = self(state);
                uint64_t   value  = 0;
                const auto status = sceneApi().node_owner_id(script->m_SceneFrame,
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

            static int nodeType(lua_State* state)
            {
                auto*       script = self(state);
                const char* data   = nullptr;
                uint64_t    size   = 0;
                const auto  status = sceneApi().node_type(script->m_SceneFrame,
                                                          static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                          &data,
                                                          &size);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushlstring(state, data, size);
                }
                return result(state, status);
            }

            static int setNodeOwner(lua_State* state)
            {
                auto*      script = self(state);
                const auto node   = static_cast<uint64_t>(luaL_checkinteger(state, 1));
                const auto owner  = lua_isnoneornil(state, 2) ? 0 : static_cast<uint64_t>(luaL_checkinteger(state, 2));
                const auto status = sceneApi().set_node_owner(script->m_SceneFrame, node, owner);
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

            static int createNode(lua_State* state)
            {
                auto*       script = self(state);
                const auto  parent = static_cast<uint64_t>(luaL_checkinteger(state, 1));
                size_t      size   = 0;
                const char* name   = luaL_checklstring(state, 2, &size);
                uint64_t    value  = 0;
                const auto  status = sceneApi().create_node(script->m_SceneFrame, parent, name, size, &value);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(value));
                }
                return result(state, status);
            }

            static int createNode3D(lua_State* state)
            {
                auto*       script = self(state);
                const auto  parent = static_cast<uint64_t>(luaL_checkinteger(state, 1));
                size_t      size   = 0;
                const char* name   = luaL_checklstring(state, 2, &size);
                uint64_t    value  = 0;
                const auto  status = sceneApi().create_node3d(script->m_SceneFrame, parent, name, size, &value);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(value));
                }
                return result(state, status);
            }

            static int createMesh(lua_State* state)
            {
                auto*       script    = self(state);
                const auto  parent    = static_cast<uint64_t>(luaL_checkinteger(state, 1));
                size_t      nameSize  = 0;
                const char* name      = luaL_checklstring(state, 2, &nameSize);
                size_t      assetSize = 0;
                const char* assetId   = luaL_checklstring(state, 3, &assetSize);
                uint64_t    value     = 0;
                const auto  status =
                    sceneApi().create_mesh(script->m_SceneFrame, parent, name, nameSize, assetId, assetSize, &value);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(value));
                }
                return result(state, status);
            }

            static int reparentNode(lua_State* state)
            {
                auto*      script = self(state);
                const auto status =
                    sceneApi().reparent_node(script->m_SceneFrame,
                                             static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                             static_cast<uint64_t>(luaL_checkinteger(state, 2)),
                                             lua_isnoneornil(state, 3) || lua_toboolean(state, 3) ? 1 : 0);
                if (status != VULTRA_STATUS_OK)
                {
                    return luaL_error(state, "Vultra API status %d", int(status));
                }
                return 0;
            }

            static int duplicateMesh(lua_State* state)
            {
                auto*      script = self(state);
                uint64_t   value  = 0;
                const auto status = sceneApi().duplicate_mesh(script->m_SceneFrame,
                                                              static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                              static_cast<uint64_t>(luaL_checkinteger(state, 2)),
                                                              &value);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(value));
                }
                return result(state, status);
            }

            static int copyMeshModel(lua_State* state)
            {
                auto*      script = self(state);
                const auto status = sceneApi().copy_mesh_model(script->m_SceneFrame,
                                                               static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                               static_cast<uint64_t>(luaL_checkinteger(state, 2)));
                if (status != VULTRA_STATUS_OK)
                {
                    return luaL_error(state, "Vultra API status %d", int(status));
                }
                return 0;
            }

            static int setMeshModel(lua_State* state)
            {
                auto*       script = self(state);
                const auto  node   = static_cast<uint64_t>(luaL_checkinteger(state, 1));
                size_t      size   = 0;
                const char* text   = luaL_checklstring(state, 2, &size);
                const auto  status = sceneApi().set_mesh_model(script->m_SceneFrame, node, text, size);
                if (status != VULTRA_STATUS_OK)
                {
                    return luaL_error(state, "Vultra API status %d", int(status));
                }
                return 0;
            }

            static int removeNode(lua_State* state)
            {
                auto*      script = self(state);
                const auto status =
                    sceneApi().remove_node(script->m_SceneFrame, static_cast<uint64_t>(luaL_checkinteger(state, 1)));
                if (status != VULTRA_STATUS_OK)
                {
                    return luaL_error(state, "Vultra API status %d", int(status));
                }
                return 0;
            }

            static int createCamera(lua_State* state)
            {
                auto*       script = self(state);
                size_t      size   = 0;
                const auto* name   = luaL_checklstring(state, 2, &size);
                uint64_t    id     = 0;
                const auto  status = sceneApi().create_camera(script->m_SceneFrame,
                                                              static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                              name,
                                                              size,
                                                              &id);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(id));
                }
                return result(state, status);
            }

            static int cameraSettings(lua_State* state)
            {
                VultraCameraSettings value {};
                const auto status = sceneApi().camera_settings(self(state)->m_SceneFrame,
                                                               static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                               &value);
                if (status != VULTRA_STATUS_OK)
                {
                    return result(state, status);
                }
                scripting::detail::pushLuaValue(state, value);
                return 1;
            }

            static void pushProperty(lua_State* state, const nlohmann::json& value, bool arrayElement = false)
            {
                if (value.is_null())
                {
                    if (arrayElement)
                    {
                        lua_pushlightuserdata(state, nullptr);
                    }
                    else
                    {
                        lua_pushnil(state);
                    }
                }
                else if (value.is_boolean())
                {
                    lua_pushboolean(state, value.get<bool>());
                }
                else if (value.is_number_integer())
                {
                    if (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(INT64_MAX))
                    {
                        throw std::invalid_argument("Property exceeds Lua's signed integer range");
                    }
                    lua_pushinteger(state, value.get<int64_t>());
                }
                else if (value.is_number())
                {
                    lua_pushnumber(state, value.get<double>());
                }
                else if (value.is_string())
                {
                    const auto& text = value.get_ref<const std::string&>();
                    lua_pushlstring(state, text.data(), text.size());
                }
                else if (value.is_array())
                {
                    lua_createtable(state, int(value.size()), 0);
                    for (size_t index = 0; index < value.size(); ++index)
                    {
                        pushProperty(state, value[index], true);
                        lua_rawseti(state, -2, lua_Integer(index + 1));
                    }
                }
                else
                {
                    throw std::invalid_argument("Unsupported Lua property value");
                }
            }

            static nlohmann::json readProperty(lua_State* state, int index, int depth = 0)
            {
                switch (lua_type(state, index))
                {
                    case LUA_TNIL:
                        return nullptr;
                    case LUA_TBOOLEAN:
                        return lua_toboolean(state, index) != 0;
                    case LUA_TNUMBER:
                        if (lua_isinteger(state, index))
                        {
                            return lua_tointeger(state, index);
                        }
                        if (!std::isfinite(lua_tonumber(state, index)))
                        {
                            throw std::invalid_argument("Property requires finite numbers");
                        }
                        return lua_tonumber(state, index);
                    case LUA_TLIGHTUSERDATA:
                        if (!lua_touserdata(state, index))
                        {
                            return nullptr;
                        }
                        throw std::invalid_argument("Invalid Lua property sentinel");
                    case LUA_TSTRING: {
                        size_t      size = 0;
                        const auto* text = lua_tolstring(state, index, &size);
                        return std::string(text, size);
                    }
                    case LUA_TTABLE: {
                        if (depth >= 2)
                        {
                            throw std::invalid_argument("Property tables must be scalar/vector arrays");
                        }
                        index           = lua_absindex(state, index);
                        const auto size = lua_rawlen(state, index);
                        size_t     keys = 0;
                        lua_pushnil(state);
                        while (lua_next(state, index))
                        {
                            const auto key = lua_isinteger(state, -2) ? lua_tointeger(state, -2) : 0;
                            if (key < 1 || uint64_t(key) > size)
                            {
                                throw std::invalid_argument("Property tables require consecutive array indices");
                            }
                            ++keys;
                            lua_pop(state, 1);
                        }
                        if (keys != size)
                        {
                            throw std::invalid_argument("Property arrays cannot contain holes");
                        }
                        auto result = nlohmann::json::array();
                        for (size_t item = 1; item <= size; ++item)
                        {
                            lua_rawgeti(state, index, lua_Integer(item));
                            result.push_back(readProperty(state, -1, depth + 1));
                            lua_pop(state, 1);
                        }
                        return result;
                    }
                    default:
                        throw std::invalid_argument("Property requires a scalar, UUID or array");
                }
            }

            static int getProperty(lua_State* state)
            {
                auto*       script = self(state);
                const auto  node   = uint64_t(luaL_checkinteger(state, 1));
                size_t      length = 0;
                const auto* name   = luaL_checklstring(state, 2, &length);
                // Raise Lua errors only after owned C++ temporaries have unwound.
                {
                    try
                    {
                        uint64_t    size = 0;
                        const auto& api  = sceneApi();
                        if (api.read_property(script->m_SceneFrame, node, name, length, nullptr, 0, &size) !=
                            VULTRA_STATUS_OK)
                        {
                            throw std::invalid_argument("Read scene property failed");
                        }
                        std::string json(size, '\0');
                        if (api.read_property(script->m_SceneFrame, node, name, length, json.data(), size, &size) !=
                            VULTRA_STATUS_OK)
                        {
                            throw std::invalid_argument("Read scene property failed");
                        }
                        pushProperty(state, nlohmann::json::parse(json));
                        return 1;
                    }
                    catch (const std::exception& error)
                    {
                        lua_pushstring(state, error.what());
                    }
                }
                return lua_error(state);
            }

            static int actionState(lua_State* state)
            {
                size_t      size   = 0;
                const auto* name   = luaL_checklstring(state, 1, &size);
                uint64_t    value  = 0;
                const auto  status = sceneApi().action_state(self(state)->m_SceneFrame, name, size, &value);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_createtable(state, 0, 3);
                    lua_pushboolean(state, (value & 1) != 0);
                    lua_setfield(state, -2, "held");
                    lua_pushboolean(state, (value & 2) != 0);
                    lua_setfield(state, -2, "pressed");
                    lua_pushboolean(state, (value & 4) != 0);
                    lua_setfield(state, -2, "released");
                }
                return result(state, status);
            }

            static int setProperty(lua_State* state)
            {
                auto*       script = self(state);
                const auto  node   = uint64_t(luaL_checkinteger(state, 1));
                size_t      length = 0;
                const auto* name   = luaL_checklstring(state, 2, &length);
                {
                    try
                    {
                        const auto json = readProperty(state, 3).dump();
                        if (sceneApi()
                                .write_property(script->m_SceneFrame, node, name, length, json.data(), json.size()) !=
                            VULTRA_STATUS_OK)
                        {
                            throw std::invalid_argument("Write scene property failed");
                        }
                        return 0;
                    }
                    catch (const std::exception& error)
                    {
                        lua_pushstring(state, error.what());
                    }
                }
                return lua_error(state);
            }

            static int setCameraSettings(lua_State* state)
            {
                const auto value  = scripting::detail::readLuaCameraSettings(state, 2);
                const auto status = sceneApi().set_camera_settings(self(state)->m_SceneFrame,
                                                                   static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                   value);
                return result(state, status, 0);
            }

            static int setCurrentCamera(lua_State* state)
            {
                return result(state,
                              sceneApi().set_current_camera(self(state)->m_SceneFrame,
                                                            static_cast<uint64_t>(luaL_checkinteger(state, 1))),
                              0);
            }

            static int createEnvironment(lua_State* state)
            {
                size_t      size   = 0;
                const auto* name   = luaL_checklstring(state, 2, &size);
                uint64_t    id     = 0;
                const auto  status = sceneApi().create_environment(self(state)->m_SceneFrame,
                                                                   static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                   name,
                                                                   size,
                                                                   &id);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(id));
                }
                return result(state, status);
            }

            static int environmentSettings(lua_State* state)
            {
                VultraEnvironmentSettings value {};
                const auto status = sceneApi().environment_settings(self(state)->m_SceneFrame,
                                                                    static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                    &value);
                if (status != VULTRA_STATUS_OK)
                {
                    return result(state, status);
                }
                scripting::detail::pushLuaValue(state, value);
                return 1;
            }

            static int setEnvironmentSettings(lua_State* state)
            {
                const auto value = scripting::detail::readLuaEnvironmentSettings(state, 2);
                return result(state,
                              sceneApi().set_environment_settings(self(state)->m_SceneFrame,
                                                                  static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                  value),
                              0);
            }

            static int setEnvironmentAsset(lua_State* state)
            {
                size_t      size  = 0;
                const auto* asset = luaL_checklstring(state, 2, &size);
                return result(state,
                              sceneApi().set_environment_asset(self(state)->m_SceneFrame,
                                                               static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                               asset,
                                                               size),
                              0);
            }

            static int setCurrentEnvironment(lua_State* state)
            {
                return result(state,
                              sceneApi().set_current_environment(self(state)->m_SceneFrame,
                                                                 static_cast<uint64_t>(luaL_checkinteger(state, 1))),
                              0);
            }

            static int createLight(lua_State* state)
            {
                auto*       script = self(state);
                size_t      size   = 0;
                const auto* name   = luaL_checklstring(state, 2, &size);
                uint64_t    id     = 0;
                const auto  status = sceneApi().create_light(script->m_SceneFrame,
                                                             static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                             name,
                                                             size,
                                                             static_cast<uint64_t>(luaL_checkinteger(state, 3)),
                                                             &id);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(id));
                }
                return result(state, status);
            }

            static int lightKind(lua_State* state)
            {
                uint64_t   kind   = 0;
                const auto status = sceneApi().light_kind(self(state)->m_SceneFrame,
                                                          static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                          &kind);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(kind));
                }
                return result(state, status);
            }

            static int lightSettings(lua_State* state)
            {
                VultraLightSettings value {};
                const auto status = sceneApi().light_settings(self(state)->m_SceneFrame,
                                                              static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                              &value);
                if (status != VULTRA_STATUS_OK)
                {
                    return result(state, status);
                }
                scripting::detail::pushLuaValue(state, value);
                return 1;
            }

            static int setLightSettings(lua_State* state)
            {
                const auto value = scripting::detail::readLuaLightSettings(state, 2);
                return result(state,
                              sceneApi().set_light_settings(self(state)->m_SceneFrame,
                                                            static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                            value),
                              0);
            }

            static int objectType(lua_State* state)
            {
                const char* name   = nullptr;
                uint64_t    size   = 0;
                const auto  status = sceneApi().object_type(self(state)->m_SceneFrame,
                                                            uint64_t(luaL_checkinteger(state, 1)),
                                                            &name,
                                                            &size);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushlstring(state, name, size);
                }
                return result(state, status);
            }

            static int createResource(lua_State* state)
            {
                size_t      typeSize  = 0;
                size_t      stateSize = 0;
                const auto* type      = luaL_checklstring(state, 1, &typeSize);
                const auto* data      = luaL_checklstring(state, 2, &stateSize);
                uint64_t    id        = 0;
                const auto  status =
                    sceneApi().create_resource(self(state)->m_SceneFrame, type, typeSize, data, stateSize, &id);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(id));
                }
                return result(state, status);
            }

            static int findResource(lua_State* state)
            {
                size_t      size   = 0;
                const auto* asset  = luaL_checklstring(state, 1, &size);
                uint64_t    id     = 0;
                const auto  status = sceneApi().find_resource(self(state)->m_SceneFrame, asset, size, &id);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(id));
                }
                return result(state, status);
            }

            static int removeResource(lua_State* state)
            {
                return result(
                    state,
                    sceneApi().remove_resource(self(state)->m_SceneFrame, uint64_t(luaL_checkinteger(state, 1))),
                    0);
            }

            static int createMaterial(lua_State* state)
            {
                size_t      size   = 0;
                const auto* name   = luaL_checklstring(state, 1, &size);
                uint64_t    id     = 0;
                const auto  status = sceneApi().create_material(self(state)->m_SceneFrame, name, size, &id);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(id));
                }
                return result(state, status);
            }

            static int materialName(lua_State* state)
            {
                const char* name   = nullptr;
                uint64_t    size   = 0;
                const auto  status = sceneApi().material_name(self(state)->m_SceneFrame,
                                                              static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                              &name,
                                                              &size);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushlstring(state, name, size);
                }
                return result(state, status);
            }

            static int materialParameters(lua_State* state)
            {
                VultraMaterialParameters value {};
                const auto status = sceneApi().material_parameters(self(state)->m_SceneFrame,
                                                                   static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                   &value);
                if (status != VULTRA_STATUS_OK)
                {
                    return result(state, status);
                }
                scripting::detail::pushLuaValue(state, value);
                return 1;
            }

            static int setMaterialParameters(lua_State* state)
            {
                const auto value = scripting::detail::readLuaMaterialParameters(state, 2);
                return result(state,
                              sceneApi().set_material_parameters(self(state)->m_SceneFrame,
                                                                 static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                                 value),
                              0);
            }

            static int meshMaterial(lua_State* state)
            {
                uint64_t   id     = 0;
                const auto status = sceneApi().mesh_material(self(state)->m_SceneFrame,
                                                             static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                             static_cast<uint64_t>(luaL_checkinteger(state, 2)),
                                                             &id);
                if (status == VULTRA_STATUS_OK)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(id));
                }
                return result(state, status);
            }

            static int setMeshMaterial(lua_State* state)
            {
                return result(state,
                              sceneApi().set_mesh_material(self(state)->m_SceneFrame,
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 1)),
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 2)),
                                                           static_cast<uint64_t>(luaL_checkinteger(state, 3))),
                              0);
            }

            static int uiText(lua_State* state)
            {
#ifdef VULTRA_SCRIPT_GUI
                auto*       script = self(state);
                size_t      size   = 0;
                const char* data   = luaL_checklstring(state, 1, &size);
                const auto  status = uiApi().text(script->m_UiFrame, data, size);
                if (status != VULTRA_STATUS_OK)
                {
                    return luaL_error(state, "Vultra API status %d", int(status));
                }
                return 0;
#else
                return luaL_error(state, "Lua UI requires the api_ui feature");
#endif
            }

            static int uiButton(lua_State* state)
            {
#ifdef VULTRA_SCRIPT_GUI
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
#else
                return luaL_error(state, "Lua UI requires the api_ui feature");
#endif
            }

            static constexpr const char* kLuaPrelude = R"lua(
local scene_api, gui_api = scene, gui
local node = {}
node.__index = node
function node:get_child(index) return setmetatable({ id = scene_api.child_id(self.id, index) }, node) end
function node:child_count() return scene_api.child_count(self.id) end
function node:name() return scene_api.node_name(self.id) end
function node:get_property(name) return scene_api.get_property(self.id, name) end
function node:set_property(name, value) return scene_api.set_property(self.id, name, value) end
function node:get_class() return scene_api.node_type(self.id) end
function node:get_process_mode() local mode = scene_api.node_execution(self.id); return mode end
function node:get_process_priority() local _, priority = scene_api.node_execution(self.id); return priority end
function node:is_inside_tree() local _, _, inside = scene_api.node_execution(self.id); return inside end
function node:is_queued_for_deletion() local _, _, _, queued = scene_api.node_execution(self.id); return queued end
function node:set_process_mode(mode) return scene_api.set_node_process_mode(self.id, mode) end
function node:set_process_priority(priority) return scene_api.set_node_process_priority(self.id, priority) end
function node:queue_free() return scene_api.queue_free(self.id) end
function node:get_parent()
    local id = scene_api.node_parent_id(self.id)
    if id == 0 then return nil end
    return setmetatable({ id = id }, node)
end
function node:get_owner()
    local id = scene_api.node_owner_id(self.id)
    if id == 0 then return nil end
    return setmetatable({ id = id }, node)
end
function node:set_owner(owner) return scene_api.set_node_owner(self.id, owner and owner.id or 0) end
function node:position() return scene_api.node_translation(self.id) end
function node:set_position(x, y, z) return scene_api.set_node_translation(self.id, x, y, z) end
function node:create_child(name)
    return setmetatable({ id = scene_api.create_node(self.id, name) }, node)
end
function node:create_child_3d(name)
    return setmetatable({ id = scene_api.create_node3d(self.id, name) }, node)
end
function node:create_mesh_child(name, asset_id)
    return setmetatable({ id = scene_api.create_mesh(self.id, name, asset_id) }, node)
end
function node:reparent(parent, keep_global) return scene_api.reparent_node(self.id, parent.id, keep_global) end
function node:duplicate_mesh(parent)
    return setmetatable({ id = scene_api.duplicate_mesh(self.id, parent.id) }, node)
end
function node:copy_mesh_model(source) return scene_api.copy_mesh_model(self.id, source.id) end
function node:set_mesh_model(asset_id) return scene_api.set_mesh_model(self.id, asset_id) end
function node:remove() return scene_api.remove_node(self.id) end
function node:create_camera_child(name)
    return setmetatable({ id = scene_api.create_camera(self.id, name) }, node)
end
local light_kinds = { directional = 0, point = 1, spot = 2 }
function node:create_light_child(name, kind)
    assert(light_kinds[kind] ~= nil, "Unknown light kind")
    return setmetatable({ id = scene_api.create_light(self.id, name, light_kinds[kind]) }, node)
end
function node:camera_settings() return scene_api.camera_settings(self.id) end
function node:set_camera_settings(settings) return scene_api.set_camera_settings(self.id, settings) end
function node:make_current() return scene_api.set_current_camera(self.id) end
function node:light_kind() return ({ "directional", "point", "spot" })[scene_api.light_kind(self.id) + 1] end
function node:create_environment_child(name)
    return setmetatable({ id = scene_api.create_environment(self.id, name) }, node)
end
function node:environment_settings() return scene_api.environment_settings(self.id) end
function node:set_environment_settings(settings) return scene_api.set_environment_settings(self.id, settings) end
function node:set_environment_asset(asset_id) return scene_api.set_environment_asset(self.id, asset_id or "") end
function node:make_environment_current() return scene_api.set_current_environment(self.id) end
function node:light_settings() return scene_api.light_settings(self.id) end
function node:set_light_settings(settings) return scene_api.set_light_settings(self.id, settings) end
local world = { null = scene_api.null }
local resource = {}
resource.__index = resource
function resource:get_class() return scene_api.object_type(self.id) end
function resource:get_property(name) return scene_api.get_property(self.id, name) end
function resource:set_property(name, value) return scene_api.set_property(self.id, name, value) end
function resource:remove() return scene_api.remove_resource(self.id) end
local material = setmetatable({}, { __index = resource })
material.__index = material
function material:name() return scene_api.material_name(self.id) end
function material:parameters() return scene_api.material_parameters(self.id) end
function material:set_parameters(parameters) return scene_api.set_material_parameters(self.id, parameters) end
function node:material(slot)
    local id = scene_api.mesh_material(self.id, slot)
    if id == 0 then return nil end
    return setmetatable({ id = id }, material)
end
function node:set_material(slot, value) return scene_api.set_mesh_material(self.id, slot, value and value.id or 0) end
local function wrap_resource(id)
    if id == 0 then return nil end
    return setmetatable({ id = id }, scene_api.object_type(id) == "MaterialResource" and material or resource)
end
function world:create_resource(type_name, state_json)
    return wrap_resource(scene_api.create_resource(type_name, state_json or "{}"))
end
function world:find_resource(asset_id) return wrap_resource(scene_api.find_resource(asset_id)) end
function world:create_material(name)
    return setmetatable({ id = scene_api.create_material(name) }, material)
end
function world:root() return setmetatable({ id = scene_api.root_id() }, node) end
function world:action(name) return scene_api.action(name) end
function world:is_paused() local paused = scene_api.execution(); return paused end
function world:physics_frame() local _, tick = scene_api.execution(); return tick end
function world:time_scale() local _, _, scale = scene_api.execution(); return scale end
function world:set_paused(paused) return scene_api.set_paused(paused) end
function world:set_time_scale(scale) return scene_api.set_time_scale(scale) end
function world:clear_current_camera() return scene_api.set_current_camera(0) end
function world:clear_current_environment() return scene_api.set_current_environment(0) end
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
            SceneAccess      m_Access;
            VultraSceneFrame m_SceneFrame {};
            VultraUiFrame    m_UiFrame {};
        };
    } // namespace

    std::unique_ptr<ScriptInstance>
    loadLuaScript(const std::filesystem::path& path, SceneTree& scene, ObjectId node, const ProjectManifest* project)
    {
        return std::make_unique<LuaScript>(path, scene, node, project);
    }
} // namespace vultra
