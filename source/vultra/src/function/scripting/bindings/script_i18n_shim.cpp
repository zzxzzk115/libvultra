#include "vultra/function/scripting/bindings/script_i18n_shim.hpp"

#include "vultra/core/services/i18n_service.hpp"

namespace vultra
{
    std::string i18nTr(ScriptContext& ctx, const std::string& key)
    {
        return ctx.i18nService ? std::string(ctx.i18nService->translate(key)) : key;
    }

    bool i18nSetLanguage(ScriptContext& ctx, const std::string& locale)
    {
        return ctx.i18nService ? ctx.i18nService->setLanguage(locale) : false;
    }

    std::string i18nLanguage(ScriptContext& ctx)
    {
        return ctx.i18nService ? std::string(ctx.i18nService->currentLanguage()) : std::string {};
    }

    sol::table i18nLanguages(ScriptContext& ctx, sol::this_state luaState)
    {
        sol::state_view lua(luaState);
        sol::table      result = lua.create_table();
        if (!ctx.i18nService)
            return result;
        int index = 1;
        for (const auto& locale : ctx.i18nService->availableLanguages())
            result[index++] = locale;
        return result;
    }

    std::string i18nDisplayName(ScriptContext& ctx, const std::string& locale)
    {
        return ctx.i18nService ? ctx.i18nService->displayName(locale) : std::string {};
    }

    void i18nSetPseudolocalize(ScriptContext& ctx, bool enabled)
    {
        if (ctx.i18nService)
            ctx.i18nService->setPseudolocalize(enabled);
    }
} // namespace vultra
