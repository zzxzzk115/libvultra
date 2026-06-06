#pragma once

#include "vultra/core/engine/engine_subsystem.hpp"
#include "vultra/core/services/i18n_service.hpp"

#include <map>
#include <unordered_map>
#include <unordered_set>

namespace vultra
{
    // Core localization subsystem. Registers II18nService and publishes itself as the ambient
    // translator (vultra/core/i18n/i18n.hpp). Holds a set of catalog sources (engine/editor/game)
    // that stack by priority into one merged map for the active locale.
    class I18nSystem final : public EngineSubsystem, public II18nService
    {
    public:
        ENGINE_SUBSYSTEM(I18nSystem)

        const char* translate(std::string_view key) noexcept override;
        const char* translateId(std::string_view key, std::string_view stableId) noexcept override;

        bool registerCatalog(std::string_view locale,
                             std::string      jsonText,
                             std::string_view domain,
                             int              priority) override;

        bool registerCompressedCatalog(std::string_view     locale,
                                       const unsigned char* lz4Data,
                                       std::size_t          lz4Size,
                                       std::size_t          rawSize,
                                       std::string_view     domain,
                                       int                  priority) override;

        bool                     setLanguage(std::string_view locale) override;
        std::string_view         currentLanguage() const override { return m_ActiveLocale; }
        std::vector<std::string> availableLanguages() const override;
        std::string              displayName(std::string_view locale) const override;

        uint64_t onLanguageChanged(LanguageChangedCallback cb) override;
        void     removeLanguageChangedListener(uint64_t token) override;

        std::vector<std::string> missingKeys() const override;
        void                     setPseudolocalize(bool enabled) override;

    protected:
        bool onInit() override;
        void onShutdown() override;

    private:
        // Heterogeneous hashing so find(string_view) does not allocate a std::string on a hit.
        struct TransparentStringHash
        {
            using is_transparent = void;
            std::size_t operator()(std::string_view s) const noexcept
            {
                return std::hash<std::string_view> {}(s);
            }
        };
        using Catalog = std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>;

        struct CatalogSource
        {
            std::string                  domain;
            int                          priority {0};
            std::map<std::string, Catalog> byLocale; // locale -> flattened key/value map
        };

        struct IdEntry
        {
            std::uint64_t generation {0};
            std::string   text; // "value###id"
        };

        CatalogSource& sourceFor(std::string_view domain, int priority);
        void           rebuildActive();

        static constexpr const char* kDefaultLocale = "en"; // fallback when the active locale lacks a key

        std::vector<CatalogSource>      m_Sources;
        Catalog                         m_Active;       // merged view for m_ActiveLocale
        Catalog                         m_English;      // merged view for kDefaultLocale (fallback)
        std::unordered_set<std::string> m_Missing;      // keys that missed in BOTH; node-stable + audit log
        std::unordered_set<std::string> m_Pseudoized;   // node-stable backing for pseudolocalized values
        std::string                     m_ActiveLocale {"en"};
        std::map<std::string, std::string> m_DisplayNames; // locale -> human name (from @meta)

        // translateId cache, invalidated by bumping m_Generation on every language switch.
        std::unordered_map<std::string, IdEntry> m_IdCache;
        std::uint64_t                            m_Generation {1};

        std::map<std::uint64_t, LanguageChangedCallback> m_Listeners;
        std::uint64_t                                    m_NextToken {1};

        bool m_Pseudo {false};
    };
} // namespace vultra
