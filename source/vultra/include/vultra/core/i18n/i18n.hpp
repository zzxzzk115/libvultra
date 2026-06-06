#pragma once

#include "vultra/core/base/api.hpp"

#include <fmt/core.h>

#include <string>
#include <string_view>
#include <utility>

namespace vultra
{
    class II18nService;

    namespace i18n
    {
        // The active translator is a single process-wide non-owning pointer, set by I18nSystem in
        // onInit() and cleared in onShutdown(). One symbol in the vultra library (not a header-inline
        // static), so every translation unit in every target sees the same instance. This is the
        // ambient access path for deep UI draw code that has no EngineContext in hand; the service is
        // still the source of truth for lifecycle and game-facing registration.
        VULTRA_API void          setActiveTranslator(II18nService* translator) noexcept;
        VULTRA_API II18nService* activeTranslator() noexcept;

        // Translate a dotted key to a stable const char* (frame-stable; valid until setLanguage).
        // Never null: with no active translator (pre-init / tests) the key itself is returned.
        VULTRA_API const char* tr(std::string_view key) noexcept;

        // tr() plus a stable "###id" suffix so a label that doubles as an ImGui widget id keeps a
        // language-invariant id. e.g. trId("window.inspector.title", "InspectorWindow").
        VULTRA_API const char* trId(std::string_view key, std::string_view stableId) noexcept;

        // Translate a key whose value is a fmt format string, then format it with args. Returns by
        // value (owns its storage), so it's for transient use (e.g. an ImGui::Text argument).
        template<typename... Args>
        std::string trf(std::string_view key, Args&&... args)
        {
            return fmt::format(fmt::runtime(tr(key)), std::forward<Args>(args)...);
        }
    } // namespace i18n

    // Pulled into the vultra namespace so UI code can write tr("...") after including this header.
    using i18n::tr;
    using i18n::trf;
    using i18n::trId;
} // namespace vultra
