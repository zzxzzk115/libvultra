#pragma once

#include "vultra/function/scripting/internal/internal_script.hpp"

#include <string>
#include <unordered_map>

namespace vultra
{
    class InternalScriptRegistry
    {
    public:
        static InternalScriptRegistry& getInstance()
        {
            static InternalScriptRegistry instance;
            return instance;
        }

        void registerScript(const std::string& name, Ref<InternalScriptInstance> script) { m_Scripts[name] = script; }

        Ref<InternalScriptInstance> getScript(const std::string& name)
        {
            auto it = m_Scripts.find(name);
            if (it != m_Scripts.end())
            {
                return it->second;
            }
            return nullptr;
        }

    private:
        InternalScriptRegistry() = default;

        std::unordered_map<std::string, Ref<InternalScriptInstance>> m_Scripts;
    };

#define VULTRA_REGISTER_INTERNAL_SCRIPT(ScriptClass) \
    struct ScriptClass##Registrar \
    { \
        ScriptClass##Registrar() \
        { \
            InternalScriptRegistry::getInstance().registerScript(#ScriptClass, std::make_shared<ScriptClass>()); \
        } \
    }; \
    static ScriptClass##Registrar s_##ScriptClass##Registrar;
} // namespace vultra