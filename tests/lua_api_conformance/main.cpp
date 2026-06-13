// Lua API conformance harness.
//
// Boots a headless Lua state, registers the full script binding surface with a
// null-service ScriptContext (binding registration only captures service
// pointers; it must not dereference them), then runs conformance.lua which
// asserts the rules from doc/lua_api_design.md and diffs the live surface
// against tools/lua-stubs/vultra.lua.
//
// Usage:
//   test-lua-api-conformance           run the checks (exit 0 = conformant)
//   test-lua-api-conformance --dump    print a fresh exceptions.lua baseline

#include <vultra/function/scripting/bindings/script_imgui_binding.hpp>
#include <vultra/function/scripting/script_binding.hpp>
#include <vultra/function/scripting/script_context.hpp>

#include <sol/sol.hpp>

#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    fs::path findRepoRoot()
    {
        fs::path probe = fs::current_path();
        while (true)
        {
            if (fs::exists(probe / "tests" / "lua_api_conformance" / "conformance.lua"))
                return probe;
            fs::path parent = probe.parent_path();
            if (parent == probe)
                break;
            probe = parent;
        }
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    const bool dumpMode = argc > 1 && std::string_view(argv[1]) == "--dump";

    const fs::path repoRoot = findRepoRoot();
    if (repoRoot.empty())
    {
        std::cerr << "[conformance] could not locate repo root (run from inside the repository)\n";
        return 2;
    }

    sol::state lua;
    lua.open_libraries(sol::lib::base,
                       sol::lib::package,
                       sol::lib::string,
                       sol::lib::table,
                       sol::lib::math,
                       sol::lib::coroutine,
                       sol::lib::os,
                       sol::lib::io,
                       sol::lib::debug);

    // Snapshot the standard-library globals before binding registration so the
    // checker can isolate the engine-added surface.
    std::vector<std::string> baseline;
    for (const auto& [key, value] : lua.globals())
    {
        if (key.is<std::string>())
            baseline.push_back(key.as<std::string>());
    }

    vultra::ScriptContext ctx {};
    vultra::registerScriptBindings(lua, ctx);

    // normally service-gated (registerScriptImGuiBindings no-ops without an
    // IImGuiService); force the generated table in so the checker can verify
    // registration and the out-of-frame guard
    vultra::registerGeneratedImGuiBindings(lua);

    sol::table conf            = lua.create_named_table("Conformance");
    conf["repoRoot"]           = repoRoot.generic_string();
    conf["stubPath"]           = (repoRoot / "tools" / "lua-stubs" / "vultra.lua").generic_string();
    conf["exceptionsPath"]     = (repoRoot / "tests" / "lua_api_conformance" / "exceptions.lua").generic_string();
    conf["dump"]               = dumpMode;
    sol::table baselineTable   = lua.create_table(static_cast<int>(baseline.size()), 0);
    for (size_t i = 0; i < baseline.size(); ++i)
        baselineTable[i + 1] = baseline[i];
    conf["baselineGlobals"] = baselineTable;

    const fs::path checker = repoRoot / "tests" / "lua_api_conformance" / "conformance.lua";
    sol::protected_function_result result =
        lua.safe_script_file(checker.generic_string(), sol::script_pass_on_error);
    if (!result.valid())
    {
        sol::error err = result;
        std::cerr << "[conformance] checker error: " << err.what() << "\n";
        return 2;
    }

    const int failures = result.get<int>();
    return failures > 0 ? 1 : 0;
}
