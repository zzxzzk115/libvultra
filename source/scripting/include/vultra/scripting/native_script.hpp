#pragma once

#include <vultra/api/native_plugin.h>

#include <concepts>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vultra::scripting
{
    struct ActionState
    {
        bool held;
        bool pressed;
        bool released;
    };

    struct NativeScriptInit
    {
        uint64_t    nodeId;
        const char* typeName;
        uint64_t    typeNameSize;
    };

    struct Float3
    {
        float x;
        float y;
        float z;
    };

    using CameraSettings      = VultraCameraSettings;
    using LightSettings       = VultraLightSettings;
    using MaterialParameters  = VultraMaterialParameters;
    using EnvironmentSettings = VultraEnvironmentSettings;

    enum class ProcessMode : uint64_t
    {
        eInherit,
        ePausable,
        eWhenPaused,
        eAlways,
        eDisabled
    };

    enum class LightKind : uint64_t
    {
        eDirectional,
        ePoint,
        eSpot
    };

    class NativeScript;
    class SceneMaterial;

    struct NativeScriptClass
    {
        std::string_view name;
        std::unique_ptr<NativeScript> (*create)();
    };

    class SceneNode
    {
    public:
        Float3                       position() const;
        void                         setPosition(Float3 position) const;
        SceneNode                    createChild(std::string_view name) const;
        SceneNode                    createChild3D(std::string_view name) const;
        SceneNode                    createMeshChild(std::string_view name, std::string_view assetId) const;
        SceneNode                    createCameraChild(std::string_view name) const;
        SceneNode                    createLightChild(std::string_view name, LightKind kind) const;
        SceneNode                    createEnvironmentChild(std::string_view name) const;
        EnvironmentSettings          environmentSettings() const;
        void                         setEnvironmentSettings(EnvironmentSettings settings) const;
        void                         setEnvironmentAsset(std::string_view assetId) const;
        void                         makeEnvironmentCurrent() const;
        CameraSettings               cameraSettings() const;
        void                         setCameraSettings(CameraSettings settings) const;
        void                         makeCurrent() const;
        LightKind                    lightKind() const;
        LightSettings                lightSettings() const;
        void                         setLightSettings(LightSettings settings) const;
        SceneNode                    duplicateMesh(SceneNode parent) const;
        void                         reparent(SceneNode newParent, bool keepGlobalTransform = true) const;
        void                         copyMeshModel(SceneNode source) const;
        void                         setMeshModel(std::string_view assetId) const;
        std::optional<SceneMaterial> material(uint64_t slot) const;
        void                         setMaterial(uint64_t slot, const SceneMaterial& material) const;
        void                         clearMaterial(uint64_t slot) const;
        void                         remove() const;
        SceneNode                    child(uint64_t index) const;
        uint64_t                     childCount() const;
        std::string_view             name() const;
        std::string                  propertyJson(std::string_view name) const;
        void                         setPropertyJson(std::string_view name, std::string_view json) const;
        std::string_view             typeName() const;
        std::optional<SceneNode>     parent() const;
        std::optional<SceneNode>     owner() const;
        void                         setOwner(std::optional<SceneNode> owner) const;
        ProcessMode                  processMode() const;
        void                         setProcessMode(ProcessMode mode) const;
        int32_t                      processPriority() const;
        void                         setProcessPriority(int32_t priority) const;
        bool                         isInsideTree() const;
        bool                         isQueuedForDeletion() const;
        void                         queueFree() const;

    private:
        friend class NativeScript;

        SceneNode(const NativeScript& script, uint64_t id) :
            m_Script(script),
            m_Id(id)
        {
        }

        const NativeScript& m_Script;
        uint64_t            m_Id;
    };

    class SceneMaterial
    {
    public:
        MaterialParameters parameters() const;
        void               setParameters(MaterialParameters parameters) const;
        std::string_view   name() const;
        void               remove() const;

    private:
        friend class NativeScript;
        friend class SceneNode;

        SceneMaterial(const NativeScript& script, uint64_t id) :
            m_Script(script),
            m_Id(id)
        {
        }

        const NativeScript& m_Script;
        uint64_t            m_Id;
    };

    class EditorGui
    {
    public:
        void text(std::string_view message) const
        {
            check(m_Api.text(m_Frame, message.data(), message.size()));
        }

        bool button(std::string_view label) const
        {
            uint8_t clicked = 0;
            check(m_Api.button(m_Frame, label.data(), label.size(), &clicked));
            return clicked != 0;
        }

    private:
        friend class NativeScript;

        EditorGui(const VultraUiApi& api, VultraUiFrame frame) :
            m_Api(api),
            m_Frame(frame)
        {
        }

        static void check(VultraStatus status)
        {
            if (status != VULTRA_STATUS_OK)
            {
                throw std::runtime_error("Vultra UI API call failed: " + std::to_string(int(status)));
            }
        }

        const VultraUiApi& m_Api;
        VultraUiFrame      m_Frame;
    };

    class NativeScript
    {
    public:
        virtual ~NativeScript() = default;

        virtual void onEnterTree()
        {
        }

        virtual void onReady()
        {
        }

        virtual void onPhysicsProcess(float deltaSeconds)
        {
        }

        virtual void onProcess(float deltaSeconds)
        {
        }

        virtual void onGui(const EditorGui& gui)
        {
        }

        virtual void onExitTree()
        {
        }

        ActionState action(std::string_view name) const
        {
            uint64_t value = 0;
            check(m_Host.scene->action_state(m_Frame, name.data(), name.size(), &value));
            return {(value & 1) != 0, (value & 2) != 0, (value & 4) != 0};
        }

    protected:
        SceneMaterial createMaterial(std::string_view name) const;

        bool paused() const
        {
            uint64_t value = 0;
            check(m_Host.scene->paused(m_Frame, &value));
            return value != 0;
        }

        void setPaused(bool paused) const
        {
            check(m_Host.scene->set_paused(m_Frame, paused));
        }

        uint64_t physicsFrame() const
        {
            uint64_t value = 0;
            check(m_Host.scene->physics_frame(m_Frame, &value));
            return value;
        }

        double timeScale() const
        {
            double value = 0;
            check(m_Host.scene->time_scale(m_Frame, &value));
            return value;
        }

        void setTimeScale(double scale) const
        {
            check(m_Host.scene->set_time_scale(m_Frame, scale));
        }

        void clearCurrentCamera() const
        {
            check(m_Host.scene->set_current_camera(m_Frame, 0));
        }

        void clearCurrentEnvironment() const
        {
            check(m_Host.scene->set_current_environment(m_Frame, 0));
        }

        SceneNode actor() const
        {
            return {*this, m_NodeId};
        }

        SceneNode sceneRoot() const
        {
            uint64_t id = 0;
            check(m_Host.scene->root_id(m_Frame, &id));
            return {*this, id};
        }

    private:
        friend class SceneNode;
        friend class SceneMaterial;
        friend VultraStatus
        initializeNativeModule(const VultraHostApi*, VultraPluginApi*, std::span<const NativeScriptClass>);

        static void check(VultraStatus status)
        {
            if (status != VULTRA_STATUS_OK)
            {
                throw std::runtime_error("Vultra scene API call failed: " + std::to_string(int(status)));
            }
        }

        void attach(const VultraHostApi& host, uint64_t nodeId)
        {
            m_Host         = host;
            m_Host.scripts = nullptr;
            m_NodeId       = nodeId;
        }

        static VultraStatus update(void* userData, VultraSceneFrame frame, float deltaSeconds)
        {
            auto& script   = *static_cast<NativeScript*>(userData);
            script.m_Frame = frame;
            try
            {
                script.onProcess(deltaSeconds);
                script.m_Frame = {};
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native script update: %s\n", error.what());
                script.m_Frame = {};
                return VULTRA_STATUS_ERROR;
            }
        }

        static VultraStatus gui(void* userData, VultraUiFrame frame)
        {
            auto& script = *static_cast<NativeScript*>(userData);
            if (!script.m_Host.ui)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            try
            {
                script.onGui(EditorGui(*script.m_Host.ui, frame));
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native script UI: %s\n", error.what());
                return VULTRA_STATUS_ERROR;
            }
        }

        static VultraStatus stop(void* userData)
        {
            std::unique_ptr<NativeScript> script(static_cast<NativeScript*>(userData));
            return VULTRA_STATUS_OK;
        }

        static VultraStatus notify(void* userData, VultraSceneFrame frame, uint32_t event, float deltaSeconds)
        {
            auto& script   = *static_cast<NativeScript*>(userData);
            script.m_Frame = frame;
            try
            {
                switch (event)
                {
                    case VULTRA_SCENE_ENTER_TREE:
                        script.onEnterTree();
                        break;
                    case VULTRA_SCENE_READY:
                        script.onReady();
                        break;
                    case VULTRA_SCENE_PHYSICS_PROCESS:
                        script.onPhysicsProcess(deltaSeconds);
                        break;
                    case VULTRA_SCENE_EXIT_TREE:
                        script.onExitTree();
                        break;
                    case VULTRA_SCENE_RELOADED:
                        break;
                    default:
                        script.m_Frame = {};
                        return VULTRA_STATUS_INVALID_ARGUMENT;
                }
                script.m_Frame = {};
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native script scene callback: %s\n", error.what());
                script.m_Frame = {};
                return VULTRA_STATUS_ERROR;
            }
        }

        VultraHostApi    m_Host {};
        VultraSceneFrame m_Frame {};
        uint64_t         m_NodeId = 0;
    };

    inline SceneMaterial NativeScript::createMaterial(std::string_view name) const
    {
        uint64_t id = 0;
        check(m_Host.scene->create_material(m_Frame, name.data(), name.size(), &id));
        return {*this, id};
    }

    inline ProcessMode SceneNode::processMode() const
    {
        uint64_t value = 0;
        NativeScript::check(m_Script.m_Host.scene->node_process_mode(m_Script.m_Frame, m_Id, &value));
        return static_cast<ProcessMode>(value);
    }

    inline void SceneNode::setProcessMode(ProcessMode mode) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->set_node_process_mode(m_Script.m_Frame, m_Id, static_cast<uint64_t>(mode)));
    }

    inline int32_t SceneNode::processPriority() const
    {
        int32_t value = 0;
        NativeScript::check(m_Script.m_Host.scene->node_process_priority(m_Script.m_Frame, m_Id, &value));
        return value;
    }

    inline void SceneNode::setProcessPriority(int32_t priority) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_node_process_priority(m_Script.m_Frame, m_Id, priority));
    }

    inline bool SceneNode::isInsideTree() const
    {
        uint64_t value = 0;
        NativeScript::check(m_Script.m_Host.scene->node_inside_tree(m_Script.m_Frame, m_Id, &value));
        return value != 0;
    }

    inline bool SceneNode::isQueuedForDeletion() const
    {
        uint64_t value = 0;
        NativeScript::check(m_Script.m_Host.scene->node_queued_for_deletion(m_Script.m_Frame, m_Id, &value));
        return value != 0;
    }

    inline void SceneNode::queueFree() const
    {
        NativeScript::check(m_Script.m_Host.scene->queue_free(m_Script.m_Frame, m_Id));
    }

    inline MaterialParameters SceneMaterial::parameters() const
    {
        MaterialParameters value {};
        NativeScript::check(m_Script.m_Host.scene->material_parameters(m_Script.m_Frame, m_Id, &value));
        return value;
    }

    inline void SceneMaterial::setParameters(MaterialParameters parameters) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_material_parameters(m_Script.m_Frame, m_Id, parameters));
    }

    inline std::string_view SceneMaterial::name() const
    {
        const char* data = nullptr;
        uint64_t    size = 0;
        NativeScript::check(m_Script.m_Host.scene->material_name(m_Script.m_Frame, m_Id, &data, &size));
        return {data, size};
    }

    inline void SceneMaterial::remove() const
    {
        NativeScript::check(m_Script.m_Host.scene->remove_material(m_Script.m_Frame, m_Id));
    }

    inline std::optional<SceneMaterial> SceneNode::material(uint64_t slot) const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->mesh_material(m_Script.m_Frame, m_Id, slot, &id));
        if (id == 0)
        {
            return std::nullopt;
        }
        return SceneMaterial(m_Script, id);
    }

    inline void SceneNode::setMaterial(uint64_t slot, const SceneMaterial& material) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_mesh_material(m_Script.m_Frame, m_Id, slot, material.m_Id));
    }

    inline void SceneNode::clearMaterial(uint64_t slot) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_mesh_material(m_Script.m_Frame, m_Id, slot, 0));
    }

    inline Float3 SceneNode::position() const
    {
        VultraSceneTranslation value {};
        NativeScript::check(m_Script.m_Host.scene->node_translation(m_Script.m_Frame, m_Id, &value));
        return {value.x, value.y, value.z};
    }

    inline void SceneNode::setPosition(Float3 position) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->set_node_translation(m_Script.m_Frame, m_Id, {position.x, position.y, position.z}));
    }

    inline std::string_view SceneNode::typeName() const
    {
        const char* data = nullptr;
        uint64_t    size = 0;
        NativeScript::check(m_Script.m_Host.scene->node_type(m_Script.m_Frame, m_Id, &data, &size));
        return {data, size};
    }

    inline std::optional<SceneNode> SceneNode::parent() const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->node_parent_id(m_Script.m_Frame, m_Id, &id));
        return id ? std::optional<SceneNode>(SceneNode(m_Script, id)) : std::nullopt;
    }

    inline std::optional<SceneNode> SceneNode::owner() const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->node_owner_id(m_Script.m_Frame, m_Id, &id));
        return id ? std::optional<SceneNode>(SceneNode(m_Script, id)) : std::nullopt;
    }

    inline void SceneNode::setOwner(std::optional<SceneNode> owner) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_node_owner(m_Script.m_Frame, m_Id, owner ? owner->m_Id : 0));
    }

    inline SceneNode SceneNode::createChild(std::string_view name) const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->create_node(m_Script.m_Frame, m_Id, name.data(), name.size(), &id));
        return {m_Script, id};
    }

    inline SceneNode SceneNode::createChild3D(std::string_view name) const
    {
        uint64_t id = 0;
        NativeScript::check(
            m_Script.m_Host.scene->create_node3d(m_Script.m_Frame, m_Id, name.data(), name.size(), &id));
        return {m_Script, id};
    }

    inline SceneNode SceneNode::createCameraChild(std::string_view name) const
    {
        uint64_t id = 0;
        NativeScript::check(
            m_Script.m_Host.scene->create_camera(m_Script.m_Frame, m_Id, name.data(), name.size(), &id));
        return {m_Script, id};
    }

    inline SceneNode SceneNode::createLightChild(std::string_view name, LightKind kind) const
    {
        uint64_t id = 0;
        NativeScript::check(
            m_Script.m_Host.scene->create_light(m_Script.m_Frame, m_Id, name.data(), name.size(), uint64_t(kind), &id));
        return {m_Script, id};
    }

    inline SceneNode SceneNode::createEnvironmentChild(std::string_view name) const
    {
        uint64_t id = 0;
        NativeScript::check(
            m_Script.m_Host.scene->create_environment(m_Script.m_Frame, m_Id, name.data(), name.size(), &id));
        return {m_Script, id};
    }

    inline EnvironmentSettings SceneNode::environmentSettings() const
    {
        EnvironmentSettings value {};
        NativeScript::check(m_Script.m_Host.scene->environment_settings(m_Script.m_Frame, m_Id, &value));
        return value;
    }

    inline void SceneNode::setEnvironmentSettings(EnvironmentSettings settings) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_environment_settings(m_Script.m_Frame, m_Id, settings));
    }

    inline void SceneNode::setEnvironmentAsset(std::string_view assetId) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->set_environment_asset(m_Script.m_Frame, m_Id, assetId.data(), assetId.size()));
    }

    inline void SceneNode::makeEnvironmentCurrent() const
    {
        NativeScript::check(m_Script.m_Host.scene->set_current_environment(m_Script.m_Frame, m_Id));
    }

    inline CameraSettings SceneNode::cameraSettings() const
    {
        CameraSettings settings {};
        NativeScript::check(m_Script.m_Host.scene->camera_settings(m_Script.m_Frame, m_Id, &settings));
        return settings;
    }

    inline void SceneNode::setCameraSettings(CameraSettings settings) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_camera_settings(m_Script.m_Frame, m_Id, settings));
    }

    inline void SceneNode::makeCurrent() const
    {
        NativeScript::check(m_Script.m_Host.scene->set_current_camera(m_Script.m_Frame, m_Id));
    }

    inline LightKind SceneNode::lightKind() const
    {
        uint64_t kind = 0;
        NativeScript::check(m_Script.m_Host.scene->light_kind(m_Script.m_Frame, m_Id, &kind));
        return LightKind(kind);
    }

    inline LightSettings SceneNode::lightSettings() const
    {
        LightSettings settings {};
        NativeScript::check(m_Script.m_Host.scene->light_settings(m_Script.m_Frame, m_Id, &settings));
        return settings;
    }

    inline void SceneNode::setLightSettings(LightSettings settings) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_light_settings(m_Script.m_Frame, m_Id, settings));
    }

    inline SceneNode SceneNode::createMeshChild(std::string_view name, std::string_view assetId) const
    {
        uint64_t id = 0;
        NativeScript::check(
            m_Script.m_Host.scene
                ->create_mesh(m_Script.m_Frame, m_Id, name.data(), name.size(), assetId.data(), assetId.size(), &id));
        return {m_Script, id};
    }

    inline void SceneNode::reparent(SceneNode newParent, bool keepGlobalTransform) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->reparent_node(m_Script.m_Frame, m_Id, newParent.m_Id, keepGlobalTransform ? 1 : 0));
    }

    inline SceneNode SceneNode::duplicateMesh(SceneNode parent) const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->duplicate_mesh(m_Script.m_Frame, m_Id, parent.m_Id, &id));
        return {m_Script, id};
    }

    inline void SceneNode::copyMeshModel(SceneNode source) const
    {
        NativeScript::check(m_Script.m_Host.scene->copy_mesh_model(m_Script.m_Frame, m_Id, source.m_Id));
    }

    inline void SceneNode::setMeshModel(std::string_view assetId) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->set_mesh_model(m_Script.m_Frame, m_Id, assetId.data(), assetId.size()));
    }

    inline void SceneNode::remove() const
    {
        NativeScript::check(m_Script.m_Host.scene->remove_node(m_Script.m_Frame, m_Id));
    }

    inline SceneNode SceneNode::child(uint64_t index) const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->child_id(m_Script.m_Frame, m_Id, index, &id));
        return {m_Script, id};
    }

    inline uint64_t SceneNode::childCount() const
    {
        uint64_t count = 0;
        NativeScript::check(m_Script.m_Host.scene->child_count(m_Script.m_Frame, m_Id, &count));
        return count;
    }

    inline std::string_view SceneNode::name() const
    {
        const char* data = nullptr;
        uint64_t    size = 0;
        NativeScript::check(m_Script.m_Host.scene->node_name(m_Script.m_Frame, m_Id, &data, &size));
        return {data, size};
    }

    inline std::string SceneNode::propertyJson(std::string_view name) const
    {
        uint64_t size = 0;
        NativeScript::check(
            m_Script.m_Host.scene->read_property(m_Script.m_Frame, m_Id, name.data(), name.size(), nullptr, 0, &size));
        std::string result(size, '\0');
        NativeScript::check(
            m_Script.m_Host.scene
                ->read_property(m_Script.m_Frame, m_Id, name.data(), name.size(), result.data(), result.size(), &size));
        return result;
    }

    inline void SceneNode::setPropertyJson(std::string_view name, std::string_view json) const
    {
        NativeScript::check(
            m_Script.m_Host.scene
                ->write_property(m_Script.m_Frame, m_Id, name.data(), name.size(), json.data(), json.size()));
    }

    template<std::derived_from<NativeScript> T>
    constexpr NativeScriptClass scriptClass(std::string_view name)
    {
        return {name,
                []() -> std::unique_ptr<NativeScript>
                {
                    return std::make_unique<T>();
                }};
    }

    inline VultraStatus initializeNativeModule(const VultraHostApi*               host,
                                               VultraPluginApi*                   plugin,
                                               std::span<const NativeScriptClass> classes)
    {
        // Version 1 evolves by breaking changes; larger tables must not be treated as compatible SDK layouts.
        if (!host || !plugin || host->version != VULTRA_ABI_VERSION || host->struct_size != sizeof(VultraHostApi) ||
            (host->ui && (host->ui->version != VULTRA_ABI_VERSION || host->ui->struct_size != sizeof(VultraUiApi))) ||
            !host->scene || host->scene->version != VULTRA_ABI_VERSION ||
            host->scene->struct_size != sizeof(VultraSceneApi) || plugin->version != VULTRA_ABI_VERSION ||
            plugin->struct_size != sizeof(VultraPluginApi))
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
        if (!plugin->user_data)
        {
            if (!host->scripts || host->scripts->version != VULTRA_ABI_VERSION ||
                host->scripts->struct_size != sizeof(VultraScriptRegistrationApi) || !host->scripts->register_class)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            for (const auto& entry : classes)
            {
                if (host->scripts->register_class(host->scripts->user_data, entry.name.data(), entry.name.size()) !=
                    VULTRA_STATUS_OK)
                {
                    return VULTRA_STATUS_INVALID_ARGUMENT;
                }
            }
            plugin->update = [](void*, VultraSceneFrame, float)
            {
                return VULTRA_STATUS_OK;
            };
            plugin->on_gui = [](void*, VultraUiFrame)
            {
                return VULTRA_STATUS_OK;
            };
            plugin->stop = [](void*)
            {
                return VULTRA_STATUS_OK;
            };
            return VULTRA_STATUS_OK;
        }
        const auto* request = static_cast<const NativeScriptInit*>(plugin->user_data);
        if (request->nodeId == 0 || !request->typeName || request->typeNameSize == 0)
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
        const std::string_view typeName(request->typeName, request->typeNameSize);
        for (const auto& entry : classes)
        {
            if (entry.name != typeName)
            {
                continue;
            }
            try
            {
                auto script = entry.create();
                script->attach(*host, request->nodeId);
                plugin->user_data = script.release();
                plugin->update    = &NativeScript::update;
                plugin->on_gui    = &NativeScript::gui;
                plugin->stop      = &NativeScript::stop;
                plugin->on_scene  = &NativeScript::notify;
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native script load: %s\n", error.what());
                return VULTRA_STATUS_ERROR;
            }
        }
        std::fprintf(stderr, "Native script class not registered: %.*s\n", int(typeName.size()), typeName.data());
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
} // namespace vultra::scripting

#ifdef _WIN32
#define VULTRA_NATIVE_SCRIPT_EXPORT __declspec(dllexport)
#else
#define VULTRA_NATIVE_SCRIPT_EXPORT __attribute__((visibility("default")))
#endif

// A library may register several classes; a single-class script stays one line.
#define VULTRA_NATIVE_CLASS(ScriptType) vultra::scripting::scriptClass<ScriptType>(#ScriptType)
#define VULTRA_NATIVE_MODULE(...) \
    static const vultra::scripting::NativeScriptClass   vultra_script_classes[] = {__VA_ARGS__}; \
    extern "C" VULTRA_NATIVE_SCRIPT_EXPORT VultraStatus vultra_plugin_init(const VultraHostApi* host, \
                                                                           VultraPluginApi*     plugin) \
    { \
        return vultra::scripting::initializeNativeModule(host, plugin, vultra_script_classes); \
    }
#define VULTRA_NATIVE_SCRIPT(ScriptType) VULTRA_NATIVE_MODULE(VULTRA_NATIVE_CLASS(ScriptType))
