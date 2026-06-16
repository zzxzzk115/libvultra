#pragma once

// Shim declarations for the Lua `I18n` namespace (runtime localization). Bodies in
// script_i18n_shim.cpp own the null-check and string/table marshalling; II18nService itself
// stays Lua-agnostic (its const char* / vector returns aren't serviceForward-friendly).

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"

#include <sol/sol.hpp>

#include <string>

namespace vultra
{
    struct VBIND_MODULE(name = I18n, area = i18n, service = i18nService) I18nModule
    {
    };

    // Translate a catalog key to the active language (returns the key itself if missing).
    VBIND_FN(module = I18n, name = tr, body = shim) std::string i18nTr(ScriptContext& ctx, const std::string& key);
    VBIND_FN(module = I18n, name = setLanguage, body = shim) bool i18nSetLanguage(ScriptContext& ctx, const std::string& locale);
    VBIND_FN(module = I18n, name = language, body = shim) std::string i18nLanguage(ScriptContext& ctx);
    VBIND_FN(module = I18n, name = languages, body = shim) sol::table i18nLanguages(ScriptContext& ctx, sol::this_state luaState);
    VBIND_FN(module = I18n, name = displayName, body = shim) std::string i18nDisplayName(ScriptContext& ctx, const std::string& locale);
    VBIND_FN(module = I18n, name = setPseudolocalize, body = shim) void i18nSetPseudolocalize(ScriptContext& ctx, bool enabled);
} // namespace vultra
