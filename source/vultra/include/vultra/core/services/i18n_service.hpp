#pragma once

#include <vbase/service/service_registry.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    // Localization service. Provided by I18nSystem (core subsystem). Engine, editor and games all
    // resolve user-facing text through it. See vultra/core/i18n/i18n.hpp for the ambient tr()/trf()
    // free functions that most call sites use.
    class II18nService
    {
    public:
        SERVICE_REGISTER(II18nService)

        virtual ~II18nService() = default;

        // Look up a dotted key (e.g. "menu.file.saveScene") in the active locale. Returns a stable
        // const char* (valid for the frame / until setLanguage) and never null: a miss returns the
        // key itself (interned), so untranslated keys are visible and recorded (see missingKeys()).
        virtual const char* translate(std::string_view key) noexcept = 0;

        // Like translate(), but appends a stable "###id" suffix so the visible label can change with
        // the language without changing the ImGui widget id (keeps docking/imgui.ini stable). Use for
        // any translated label that ImGui also uses as an id (windows, buttons, menu items, tabs...).
        virtual const char* translateId(std::string_view key, std::string_view stableId) noexcept = 0;

        // Register a catalog (raw JSON text) for a locale into a domain at a priority. Higher priority
        // overrides lower when keys collide; convention: engine 0, editor 100, game 200. Nested JSON
        // is flattened to dotted keys; a top-level "@meta":{"name":...} sets the locale display name.
        virtual bool registerCatalog(std::string_view locale,
                                     std::string      jsonText,
                                     std::string_view domain   = "game",
                                     int              priority = 200) = 0;

        // Register a catalog from an lz4-block-compressed JSON blob (the builtin embed format emitted
        // by builtin/xmake.lua's i18n_task: a `<sym>_lz4` / `<sym>_lz4_size` / `<sym>_size` triple).
        // Decompressed and registered like registerCatalog(); lets callers that don't link lz4 (e.g.
        // the editor) feed embedded catalogs without decoding them.
        virtual bool registerCompressedCatalog(std::string_view     locale,
                                               const unsigned char* lz4Data,
                                               std::size_t          lz4Size,
                                               std::size_t          rawSize,
                                               std::string_view     domain   = "editor",
                                               int                  priority = 100) = 0;

        virtual bool                     setLanguage(std::string_view locale)             = 0;
        virtual std::string_view         currentLanguage() const                          = 0;
        virtual std::vector<std::string> availableLanguages() const                       = 0;
        virtual std::string              displayName(std::string_view locale) const       = 0;

        using LanguageChangedCallback = std::function<void(std::string_view)>;
        virtual uint64_t onLanguageChanged(LanguageChangedCallback cb)                    = 0;
        virtual void     removeLanguageChangedListener(uint64_t token)                    = 0;

        // Diagnostics: every key that missed in the active locale since the last language switch, and
        // a pseudolocalize toggle that brackets all returned values to expose un-tr()'d literals.
        virtual std::vector<std::string> missingKeys() const = 0;
        virtual void                     setPseudolocalize(bool enabled) = 0;
    };
} // namespace vultra
