#pragma once

#include <sol/sol.hpp>

namespace vultra
{
    class ScriptEngine
    {
    public:
        bool init();
        void shutdown();

        sol::state&       lua() { return m_Lua; }
        const sol::state& lua() const { return m_Lua; }

    private:
        sol::state m_Lua;
    };
} // namespace vultra
