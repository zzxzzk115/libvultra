#pragma once

#include "vultra/core/base/base.hpp"

namespace vultra
{
    class Entity;

    class InternalScriptInstance
    {
    public:
        InternalScriptInstance() = default;
        explicit InternalScriptInstance(bool isEditorScript) : m_IsEditorScript(isEditorScript) {}
        virtual ~InternalScriptInstance() = default;

        virtual void onPreUpdate(const fsec) {}
        virtual void onUpdate(const fsec) {}
        virtual void onPhysicsUpdate(const fsec) {}
        virtual void onPostUpdate(const fsec) {}

        bool isEditorScript() const { return m_IsEditorScript; }

        void setEnabled(bool enabled) { m_Enabled = enabled; }
        bool getEnabled() const { return m_Enabled; }

    protected:
        Ref<Entity> m_OwnerEntity    = nullptr;
        bool        m_Enabled        = true;
        bool        m_IsEditorScript = false;

        friend class LogicScene;
    };

#define VULTRA_NAME_OF_SCRIPT(ScriptClass) #ScriptClass

#define VULTRA_DEFINE_INTERNAL_SCRIPT(ScriptClass) \
public: \
    static std::string getScriptName() { return #ScriptClass; }
} // namespace vultra