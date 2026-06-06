#include "vultra/core/i18n/i18n.hpp"
#include "vultra/core/services/i18n_service.hpp"

#include <string>
#include <unordered_set>

namespace vultra::i18n
{
    namespace
    {
        // Single instance in the vultra library. Set/cleared by I18nSystem; read by the free
        // functions below. UI-thread only, so no synchronization is needed.
        II18nService* g_Active = nullptr;

        // Node-stable fallback store used when there is no active translator (pre-init / tests):
        // pointers into it stay valid for the process lifetime.
        const char* internFallback(std::string s)
        {
            static std::unordered_set<std::string> store;
            return store.emplace(std::move(s)).first->c_str();
        }
    } // namespace

    void          setActiveTranslator(II18nService* translator) noexcept { g_Active = translator; }
    II18nService* activeTranslator() noexcept { return g_Active; }

    const char* tr(std::string_view key) noexcept
    {
        if (g_Active)
            return g_Active->translate(key);
        return internFallback(std::string {key});
    }

    const char* trId(std::string_view key, std::string_view stableId) noexcept
    {
        if (g_Active)
            return g_Active->translateId(key, stableId);
        std::string text {key};
        text += "###";
        text += stableId;
        return internFallback(std::move(text));
    }
} // namespace vultra::i18n
