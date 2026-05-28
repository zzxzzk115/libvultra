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
} // namespace vultra_app
