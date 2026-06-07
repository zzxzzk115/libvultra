#pragma once

#include <vasset/vasset_registry.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra_app
{
    struct ContentAssetCreator
    {
        std::string id;
        std::string menuPath;
        std::string displayName;
        std::string defaultFileName;
        std::string extension;
        vasset::VAssetType assetType {vasset::VAssetType::eUnknown};
        bool openInCodeEditor {false};
        std::function<std::string(std::string_view assetName)> makeText;
    };

    class ContentAssetRegistry final
    {
    public:
        static ContentAssetRegistry& instance();

        bool registerCreator(ContentAssetCreator creator);

        [[nodiscard]] const std::vector<ContentAssetCreator>& creators() const { return m_Creators; }
        [[nodiscard]] const ContentAssetCreator* find(std::string_view id) const;

    private:
        std::vector<ContentAssetCreator> m_Creators;
    };

    void registerBuiltinContentAssetCreators();

    // Scripted-pass Lua templates (the standard for project render passes). The
    // pass `type` comes from the asset name entered at creation.
    [[nodiscard]] std::string passTypeFromAssetName(std::string_view assetName);
    // Post-processing template with setup/execute pre-filled (the operation is
    // standard); `fragment` may be empty (a TODO placeholder is emitted).
    [[nodiscard]] std::string scriptedPostProcessPassLua(std::string_view type, std::string_view fragment);
    // Generic stub: only `type`; empty setup/execute for the user to fill.
    [[nodiscard]] std::string scriptedPassStubLua(std::string_view type);
} // namespace vultra_app
