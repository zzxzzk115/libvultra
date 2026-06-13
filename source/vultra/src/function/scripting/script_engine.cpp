#include "vultra/function/scripting/script_engine.hpp"

#include "vultra/core/base/common_context.hpp"

#include <sstream>

namespace vultra
{
    namespace
    {
        [[nodiscard]] std::string toLuaPrintString(const sol::object& obj)
        {
            switch (obj.get_type())
            {
                case sol::type::string:
                    return obj.as<std::string>();
                case sol::type::number:
                    return fmt::format("{}", obj.as<double>());
                case sol::type::boolean:
                    return obj.as<bool>() ? "true" : "false";
                case sol::type::nil:
                case sol::type::none:
                    return "nil";
                default:
                    return "<" + std::string(sol::type_name(obj.lua_state(), obj.get_type())) + ">";
            }
        }
    } // namespace

    bool ScriptEngine::init()
    {
        m_Lua.open_libraries(sol::lib::base,
                             sol::lib::math,
                             sol::lib::table,
                             sol::lib::string,
                             sol::lib::package,
                             sol::lib::coroutine,
                             sol::lib::os);

        m_Lua.set_function("print", [](sol::variadic_args args) {
            std::ostringstream stream;
            bool               first = true;
            for (const auto arg : args)
            {
                if (!first)
                    stream << '\t';
                stream << toLuaPrintString(arg);
                first = false;
            }

            VULTRA_CLIENT_INFO("[Lua] {}", stream.str());
        });


        return true;
    }


    void ScriptEngine::shutdown()
    {
        // sol2 RAII.
    }
} // namespace vultra
