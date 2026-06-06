#include "vultra/core/i18n/i18n_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/i18n/i18n.hpp"

#include <nlohmann/json.hpp>

#include <lz4.h> // decompress lz4-block-compressed embedded catalogs (builtin i18n_task)

#include <algorithm>
#include <string>

namespace vultra
{
    namespace
    {
        // Recursively flatten a nested JSON object into dotted keys (menu.file.save). String leaves
        // only; other leaf types are skipped with a warning. Keys starting with '@' at the top level
        // are reserved (e.g. "@meta") and not emitted.
        void flatten(const nlohmann::json&                                node,
                     const std::string&                                   prefix,
                     const std::function<void(std::string, std::string)>& emit)
        {
            for (auto it = node.begin(); it != node.end(); ++it)
            {
                if (prefix.empty() && !it.key().empty() && it.key().front() == '@')
                    continue; // reserved top-level metadata (e.g. "@meta")

                std::string key = prefix.empty() ? it.key() : prefix + '.' + it.key();
                if (it->is_object())
                {
                    flatten(*it, key, emit);
                }
                else if (it->is_string())
                {
                    emit(std::move(key), it->get<std::string>());
                }
                else
                {
                    VULTRA_CORE_WARN("[I18nSystem] Non-string leaf ignored: {}", key);
                }
            }
        }
    } // namespace

    bool I18nSystem::onInit()
    {
        VULTRA_CORE_INFO("[I18nSystem] Initializing...");
        ctx().services.provide<II18nService>(this);
        i18n::setActiveTranslator(this);
        VULTRA_CORE_INFO("[I18nSystem] Initialized!");
        return true;
    }

    void I18nSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[I18nSystem] Shutting down");
        i18n::setActiveTranslator(nullptr);
    }

    const char* I18nSystem::translate(std::string_view key) noexcept
    {
        // Resolve the base value: active locale first; then the English ("en") base as a fallback so
        // an untranslated key shows the default-language text rather than the raw key; finally a true
        // miss interns the key in a node-stable store and records it. Hits point into a merged catalog
        // (stable until setLanguage).
        std::string_view value;
        if (auto it = m_Active.find(key); it != m_Active.end())
        {
            value = it->second;
            if (!m_Pseudo)
                return it->second.c_str();
        }
        else if (auto en = m_English.find(key); en != m_English.end())
        {
            value = en->second;
            if (!m_Pseudo)
                return en->second.c_str();
        }
        else
        {
            const char* interned = m_Missing.emplace(key).first->c_str();
            if (!m_Pseudo)
                return interned;
            value = interned;
        }

        // Pseudolocalize: bracket the value so any on-screen text that did NOT come through tr() is
        // instantly visible as unbracketed. Stable pointer via a node-stable backing store.
        std::string pseudo;
        pseudo.reserve(value.size() + 2);
        pseudo.push_back('[');
        pseudo.append(value);
        pseudo.push_back(']');
        return m_Pseudoized.emplace(std::move(pseudo)).first->c_str();
    }

    const char* I18nSystem::translateId(std::string_view key, std::string_view stableId) noexcept
    {
        std::string cacheKey;
        cacheKey.reserve(key.size() + 1 + stableId.size());
        cacheKey.append(key);
        cacheKey.push_back('\x1f'); // unit separator: cannot occur in a key or an id
        cacheKey.append(stableId);

        auto& entry = m_IdCache[cacheKey];
        if (entry.generation != m_Generation || entry.text.empty())
        {
            entry.generation = m_Generation;
            entry.text.assign(translate(key)); // copy: pseudo/missing pointers may move on rebuild
            entry.text += "###";
            entry.text.append(stableId);
        }
        return entry.text.c_str();
    }

    I18nSystem::CatalogSource& I18nSystem::sourceFor(std::string_view domain, int priority)
    {
        for (auto& src : m_Sources)
        {
            if (src.domain == domain)
            {
                src.priority = priority; // last registration wins for the domain's priority
                return src;
            }
        }
        m_Sources.push_back(CatalogSource {std::string {domain}, priority, {}});
        return m_Sources.back();
    }

    bool I18nSystem::registerCatalog(std::string_view locale,
                                     std::string      jsonText,
                                     std::string_view domain,
                                     int              priority)
    {
        nlohmann::json json = nlohmann::json::parse(jsonText, nullptr, /*allow_exceptions*/ false);
        if (json.is_discarded() || !json.is_object())
        {
            VULTRA_CORE_ERROR("[I18nSystem] Failed to parse catalog for locale '{}' (domain '{}')",
                              std::string {locale},
                              std::string {domain});
            return false;
        }

        // Display name from reserved "@meta": { "name": "..." }.
        if (auto metaIt = json.find("@meta"); metaIt != json.end() && metaIt->is_object())
        {
            if (auto nameIt = metaIt->find("name"); nameIt != metaIt->end() && nameIt->is_string())
                m_DisplayNames[std::string {locale}] = nameIt->get<std::string>();
        }

        Catalog catalog;
        flatten(json, "", [&catalog](std::string k, std::string v) {
            catalog.insert_or_assign(std::move(k), std::move(v));
        });

        CatalogSource& src           = sourceFor(domain, priority);
        const size_t   count         = catalog.size();
        src.byLocale[std::string {locale}] = std::move(catalog);

        VULTRA_CORE_INFO("[I18nSystem] Registered {} keys for locale '{}' (domain '{}', priority {})",
                         count,
                         std::string {locale},
                         std::string {domain},
                         priority);

        if (locale == m_ActiveLocale)
            rebuildActive();

        return true;
    }

    bool I18nSystem::registerCompressedCatalog(std::string_view     locale,
                                               const unsigned char* lz4Data,
                                               std::size_t          lz4Size,
                                               std::size_t          rawSize,
                                               std::string_view     domain,
                                               int                  priority)
    {
        if (lz4Data == nullptr || lz4Size == 0 || rawSize == 0)
            return false;

        std::string  json(rawSize, '\0');
        const int    n = LZ4_decompress_safe(reinterpret_cast<const char*>(lz4Data),
                                          json.data(),
                                          static_cast<int>(lz4Size),
                                          static_cast<int>(rawSize));
        if (n != static_cast<int>(rawSize))
        {
            VULTRA_CORE_ERROR("[I18nSystem] Failed to decompress catalog for locale '{}' (domain '{}')",
                              std::string {locale},
                              std::string {domain});
            return false;
        }
        return registerCatalog(locale, std::move(json), domain, priority);
    }

    void I18nSystem::rebuildActive()
    {
        m_Active.clear();
        m_English.clear();
        m_Missing.clear();
        m_Pseudoized.clear();

        // Stack sources by ascending priority so higher-priority domains override on key collisions.
        std::vector<const CatalogSource*> ordered;
        ordered.reserve(m_Sources.size());
        for (const auto& src : m_Sources)
            ordered.push_back(&src);
        std::stable_sort(ordered.begin(), ordered.end(), [](const CatalogSource* a, const CatalogSource* b) {
            return a->priority < b->priority;
        });

        const auto mergeLocale = [&ordered](Catalog& dst, const std::string& locale) {
            for (const CatalogSource* src : ordered)
            {
                auto it = src->byLocale.find(locale);
                if (it == src->byLocale.end())
                    continue;
                for (const auto& [k, v] : it->second)
                    dst.insert_or_assign(k, v);
            }
        };

        mergeLocale(m_English, kDefaultLocale);
        mergeLocale(m_Active, m_ActiveLocale);
    }

    bool I18nSystem::setLanguage(std::string_view locale)
    {
        const bool available = std::any_of(m_Sources.begin(), m_Sources.end(), [&](const CatalogSource& src) {
            return src.byLocale.find(std::string {locale}) != src.byLocale.end();
        });
        if (!available)
        {
            VULTRA_CORE_WARN("[I18nSystem] No catalog registered for locale '{}'; keeping '{}'",
                             std::string {locale},
                             m_ActiveLocale);
            return false;
        }

        m_ActiveLocale.assign(locale);
        ++m_Generation; // invalidate the translateId cache
        rebuildActive();

        for (const auto& [token, cb] : m_Listeners)
        {
            if (cb)
                cb(m_ActiveLocale);
        }
        VULTRA_CORE_INFO("[I18nSystem] Active language set to '{}'", m_ActiveLocale);
        return true;
    }

    std::vector<std::string> I18nSystem::availableLanguages() const
    {
        std::vector<std::string> locales;
        for (const auto& src : m_Sources)
        {
            for (const auto& [locale, _] : src.byLocale)
            {
                if (std::find(locales.begin(), locales.end(), locale) == locales.end())
                    locales.push_back(locale);
            }
        }
        std::sort(locales.begin(), locales.end());
        return locales;
    }

    std::string I18nSystem::displayName(std::string_view locale) const
    {
        if (auto it = m_DisplayNames.find(std::string {locale}); it != m_DisplayNames.end())
            return it->second;
        return std::string {locale};
    }

    uint64_t I18nSystem::onLanguageChanged(LanguageChangedCallback cb)
    {
        const uint64_t token = m_NextToken++;
        m_Listeners.emplace(token, std::move(cb));
        return token;
    }

    void I18nSystem::removeLanguageChangedListener(uint64_t token) { m_Listeners.erase(token); }

    std::vector<std::string> I18nSystem::missingKeys() const
    {
        std::vector<std::string> keys {m_Missing.begin(), m_Missing.end()};
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    void I18nSystem::setPseudolocalize(bool enabled)
    {
        if (m_Pseudo == enabled)
            return;
        m_Pseudo = enabled;
        ++m_Generation;     // translateId values change shape; invalidate the cache
        m_Pseudoized.clear();
    }
} // namespace vultra
