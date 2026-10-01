#include <vultra/assets/asset_options.hpp>
#include <vultra/core/base/logger.hpp>

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("vultra-import", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Import a source model and bake its derived asset cache without a GPU");
    vultra::addAppOptions(cli);
    vultra::addAssetImportOptions(cli);
    cli.add_argument("source").help("Source .gltf, .glb, .fbx or untextured .obj");
    if (!vultra::parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    const auto source  = std::filesystem::path(cli.get<std::string>("source"));
    const auto options = vultra::getAssetImportOptions(cli);
    if (!options.reimport && vultra::isAssetCacheCurrent(source, options))
    {
        vultra::Logger::app().info("Asset cache is current: {}", source.string());
        return 0;
    }
    const auto asset = vultra::importAsset(source, options);
    if (options.cache && !vultra::isAssetCacheCurrent(source, options))
    {
        throw std::runtime_error("Asset import did not publish a valid cache: " + asset.cachePath.string());
    }
    vultra::Logger::app().info("{}: {}", asset.cacheHit ? "Cached" : "Imported", asset.cachePath.string());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
