#pragma once

#include <vultra/api/native_plugin.h>

#include <concepts>
#include <cstdint>
#include <cstdio>
#include <memory>
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

    class NativeScript;

    struct NativeScriptClass
    {
        std::string_view name;
        std::unique_ptr<NativeScript> (*create)();
    };

    class SceneNode
    {
    public:
        Float3           position() const;
        void             setPosition(Float3 position) const;
        SceneNode        child(uint64_t index) const;
        uint64_t         childCount() const;
        std::string_view name() const;

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
        if (!host || !plugin || host->version != VULTRA_ABI_VERSION || host->struct_size < sizeof(VultraHostApi) ||
            !host->ui || host->ui->version != VULTRA_ABI_VERSION || host->ui->struct_size < sizeof(VultraUiApi) ||
            !host->scene || host->scene->version != VULTRA_ABI_VERSION ||
            host->scene->struct_size < sizeof(VultraSceneApi) || plugin->version != VULTRA_ABI_VERSION ||
            plugin->struct_size < sizeof(VultraPluginApi))
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
        if (!plugin->user_data)
        {
            if (!host->scripts || host->scripts->version != VULTRA_ABI_VERSION ||
                host->scripts->struct_size < sizeof(VultraScriptRegistrationApi) || !host->scripts->register_class)
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
