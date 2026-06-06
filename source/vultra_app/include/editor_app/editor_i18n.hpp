#pragma once

#include <string>

namespace vultra
{
    class II18nService;
}

namespace vultra_app
{
    // Decode the builtin lz4-embedded editor catalogs (builtin/i18n/*.json -> i18n_headers/*) and
    // register them with the i18n service under the "editor" domain. Call once at editor startup,
    // before applying the persisted language.
    void registerBuiltinEditorCatalogs(vultra::II18nService& i18n);

    // Best-effort OS UI language as a lowercase BCP-47-ish string (e.g. "zh-cn", "en-us"); empty if
    // it cannot be determined. Windows uses GetUserDefaultLocaleName; POSIX parses LC_ALL/LANG.
    std::string detectSystemLocale();

    // Resolve the UI language to apply at startup, applied as early as possible so the launcher,
    // splash/loading screen and editor all render in it. If `savedLanguage` is non-empty (the user
    // already has a preference) it is returned unchanged. Otherwise this is a first launch: the OS
    // language is detected and mapped to a bundled locale -- Simplified Chinese -> "zh-CN",
    // everything else -> "en".
    std::string resolveStartupLocale(const std::string& savedLanguage);
} // namespace vultra_app
