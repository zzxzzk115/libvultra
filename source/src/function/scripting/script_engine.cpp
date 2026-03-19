#include "vultra/function/scripting/script_engine.hpp"

namespace vultra
{
    bool ScriptEngine::init()
    {
        m_Lua.open_libraries(sol::lib::base,
                             sol::lib::math,
                             sol::lib::table,
                             sol::lib::string,
                             sol::lib::package,
                             sol::lib::coroutine,
                             sol::lib::os);
        return true;
    }

    void ScriptEngine::shutdown()
    {
        // sol2 RAII.
    }
} // namespace vultra
