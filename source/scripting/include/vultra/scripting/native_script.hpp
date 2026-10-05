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
        void                         reparent(SceneNode newParent) const;
        void                         copyMeshModel(SceneNode source) const;
        void                         setMeshModel(std::string_view assetId) const;
        std::optional<SceneMaterial> material(uint64_t slot) const;
        void                         setMaterial(uint64_t slot, const SceneMaterial& material) const;
        void                         clearMaterial(uint64_t slot) const;
        void                         remove() const;
        SceneNode                    child(uint64_t index) const;
        uint64_t                     childCount() const;
        std::string_view             name() const;

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

        virtual void onStart()
        {
        }

        virtual void onUpdate(float deltaSeconds)
        {
        }

        virtual void onGui(const EditorGui& gui)
        {
        }

        virtual void onStop()
        {
        }

    protected:
        SceneMaterial createMaterial(std::string_view name) const;

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
                if (!script.m_Ready)
                {
                    script.onStart();
                    script.m_Ready = true;
                }
                script.onUpdate(deltaSeconds);
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
            try
            {
                if (script->m_Ready)
                {
                    script->onStop();
                }
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native script stop: %s\n", error.what());
                return VULTRA_STATUS_ERROR;
            }
        }

        VultraHostApi    m_Host {};
        VultraSceneFrame m_Frame {};
        uint64_t         m_NodeId = 0;
        bool             m_Ready  = false;
    };

    inline SceneMaterial NativeScript::createMaterial(std::string_view name) const
    {
        uint64_t id = 0;
        check(m_Host.scene->create_material(m_Frame, name.data(), name.size(), &id));
        return {*this, id};
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

    inline SceneNode SceneNode::createChild(std::string_view name) const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->create_node(m_Script.m_Frame, m_Id, name.data(), name.size(), &id));
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

    inline void SceneNode::reparent(SceneNode newParent) const
    {
        NativeScript::check(m_Script.m_Host.scene->reparent_node(m_Script.m_Frame, m_Id, newParent.m_Id));
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
            !host->ui || host->ui->version != VULTRA_ABI_VERSION || host->ui->struct_size != sizeof(VultraUiApi) ||
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
