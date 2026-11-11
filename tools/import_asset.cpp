#include <vultra/assets/asset_options.hpp>
#include <vultra/core/base/logger.hpp>

namespace
{
    std::expected<void, vultra::Error> run(int argc, char** argv)
    {
        argparse::ArgumentParser cli("vultra-import", "0.1.0", argparse::default_arguments::none);
        cli.add_description("Import a source model and bake its derived asset cache without a GPU");
        vultra::addAppOptions(cli);
        vultra::addAssetImportOptions(cli);
        cli.add_argument("source").help("Source .gltf, .glb, .fbx or untextured .obj");
        const auto parsed = vultra::parseCommandLine(cli, argc, argv);
        if (!parsed)
        {
            return std::unexpected(parsed.error());
        }
        if (!*parsed)
        {
            return {};
        }
        const auto source  = std::filesystem::path(cli.get<std::string>("source"));
        const auto options = vultra::getAssetImportOptions(cli);
        if (!options.reimport)
        {
            const auto current = vultra::isAssetCacheCurrent(source, options);
            if (!current)
            {
                return std::unexpected(current.error());
            }
            if (*current)
            {
                vultra::Logger::app().info("Asset cache is current: {}", source.string());
                return {};
            }
        }
        const auto asset = vultra::importAsset(source, options);
        if (!asset)
        {
            return std::unexpected(asset.error());
        }
        if (options.cache)
        {
            const auto current = vultra::isAssetCacheCurrent(source, options);
            if (!current)
            {
                return std::unexpected(current.error());
            }
            if (!*current)
            {
                return vultra::makeError("Asset import did not publish a valid cache: {}", asset->cachePath.string());
            }
        }
        vultra::Logger::app().info("{}: {}", asset->cacheHit ? "Cached" : "Imported", asset->cachePath.string());
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (auto result = run(argc, argv); !result)
    {
        vultra::Logger::app().error("{}", result.error().message);
        return 1;
    }
    return 0;
}
