#include "editor_app/content_asset_registry.hpp"

#include <algorithm>

namespace vultra_app
{
    namespace
    {
        std::string makeSceneText(std::string_view)
        {
            return R"([vscn]
version = 1
root    = 0
)";
        }

        std::string makeLuaScriptText(std::string_view)
        {
            return R"(function OnCreate(self)
end

function OnUpdate(self, dt)
end
)";
        }
    } // namespace

    ContentAssetRegistry& ContentAssetRegistry::instance()
    {
        static ContentAssetRegistry registry;
        return registry;
    }

    bool ContentAssetRegistry::registerCreator(ContentAssetCreator creator)
    {
        if (creator.id.empty() || creator.menuPath.empty() || creator.defaultFileName.empty() ||
            creator.extension.empty() || !creator.makeText)
        {
            return false;
        }

        auto& creators = m_Creators;
        const auto it = std::find_if(creators.begin(), creators.end(), [&](const ContentAssetCreator& existing) {
            return existing.id == creator.id;
        });
        if (it != creators.end())
            *it = std::move(creator);
        else
            creators.push_back(std::move(creator));

        std::stable_sort(creators.begin(), creators.end(), [](const auto& a, const auto& b) {
            return a.menuPath < b.menuPath;
        });
        return true;
    }

    const ContentAssetCreator* ContentAssetRegistry::find(std::string_view id) const
    {
        const auto it = std::find_if(m_Creators.begin(), m_Creators.end(), [&](const ContentAssetCreator& creator) {
            return creator.id == id;
        });
        return it == m_Creators.end() ? nullptr : &*it;
    }

    void registerBuiltinContentAssetCreators()
    {
        static bool registered = false;
        if (registered)
            return;

        auto& registry = ContentAssetRegistry::instance();
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.scene",
            .menuPath        = "Scene",
            .displayName     = "Scene",
            .defaultFileName = "NewScene.vscn",
            .extension       = ".vscn",
            .assetType       = vasset::VAssetType::eScene,
            .openInCodeEditor = false,
            .makeText        = makeSceneText,
        });
        registry.registerCreator(ContentAssetCreator {
            .id              = "vultra.lua_script",
            .menuPath        = "Script/Lua Script",
            .displayName     = "Lua Script",
            .defaultFileName = "NewScript.lua",
            .extension       = ".lua",
            .assetType       = vasset::VAssetType::eScriptLua,
            .openInCodeEditor = true,
            .makeText        = makeLuaScriptText,
        });

        registered = true;
    }
} // namespace vultra_app
