#include "editor_app/editor_i18n.hpp"

#include <vultra/core/builtin/builtin_resources.hpp>
#include <vultra/core/services/i18n_service.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vultra_app
{
    namespace
    {
        std::string toLowerAscii(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

        bool contains(std::string_view s, std::string_view needle) { return s.find(needle) != std::string_view::npos; }

        // The OS reports Simplified Chinese as zh-CN / zh-Hans(-CN) / zh-SG (and POSIX as zh_CN). Map
        // those to our only bundled Chinese catalog; Traditional (zh-TW/HK/Hant) falls through to en.
        bool isSimplifiedChinese(std::string_view loc)
        {
            if (loc.empty() || !contains(loc, "zh"))
                return false;
            if (contains(loc, "hant") || contains(loc, "-tw") || contains(loc, "_tw") || contains(loc, "-hk") ||
                contains(loc, "_hk") || contains(loc, "-mo") || contains(loc, "_mo"))
                return false;
            return contains(loc, "cn") || contains(loc, "hans") || contains(loc, "sg") || loc == "zh";
        }

        // True when an OS locale (e.g. "ja", "ja-jp", "ja_jp.utf-8") names the given ISO-639 language.
        bool isLanguage(std::string_view loc, std::string_view lang)
        {
            if (loc.size() < lang.size() || loc.compare(0, lang.size(), lang) != 0)
                return false;
            if (loc.size() == lang.size())
                return true;
            const char sep = loc[lang.size()];
            return sep == '-' || sep == '_' || sep == '.';
        }
    } // namespace

    void registerBuiltinEditorCatalogs(vultra::II18nService& i18n)
    {
        // Editor catalogs live in the builtin pack (builtin/i18n -> builtin://i18n/<locale>.json).
        // Register the raw JSON at domain "editor" / priority 100 (above engine 0, below any game
        // catalog at 200) so a game can still override editor strings if it wants.
        for (const char* locale : {"en", "zh-CN", "ja", "ko"})
        {
            std::vector<std::byte> raw;
            if (vultra::builtin::read(std::string {"i18n/"} + locale + ".json", raw) && !raw.empty())
                i18n.registerCatalog(
                    locale, std::string {reinterpret_cast<const char*>(raw.data()), raw.size()}, "editor", 100);
        }
    }

    std::string detectSystemLocale()
    {
#if defined(_WIN32)
        wchar_t buf[LOCALE_NAME_MAX_LENGTH] {};
        if (GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) > 0)
        {
            std::string out;
            for (const wchar_t* p = buf; *p != L'\0'; ++p)
                out.push_back(static_cast<char>(*p & 0x7F)); // BCP-47 tags are ASCII
            return toLowerAscii(out);
        }
        return {};
#else
        for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"})
        {
            if (const char* v = std::getenv(var); v != nullptr && v[0] != '\0')
                return toLowerAscii(v); // e.g. "zh_CN.UTF-8"
        }
        return {};
#endif
    }

    std::string resolveStartupLocale(const std::string& savedLanguage)
    {
        if (!savedLanguage.empty())
            return savedLanguage; // user already has a preference; respect it
        const std::string sys = detectSystemLocale();
        if (isSimplifiedChinese(sys))
            return "zh-CN";
        if (isLanguage(sys, "ja"))
            return "ja";
        if (isLanguage(sys, "ko"))
            return "ko";
        return "en";
    }
} // namespace vultra_app
