#include <vultra/assets/asset_options.hpp>

namespace vultra
{
    void addAssetImportOptions(argparse::ArgumentParser& cli)
    {
        cli.add_argument("--cache-dir")
            .default_value(std::string(".vultra/assets"))
            .help("Derived asset cache directory");
        cli.add_argument("--reimport").flag().help("Rebuild derived data even when the cache is current");
        cli.add_argument("--no-cache").flag().help("Import without reading or writing a cache");
        cli.add_argument("--no-mipmaps").flag().help("Import only texture level zero");
        cli.add_argument("--import-jobs")
            .default_value(uint32_t(0))
            .scan<'u', uint32_t>()
            .help("Maximum loading/import jobs (0: automatic; also limited by available memory)");
        cli.add_argument("--compression")
            .default_value(std::string("bc7-linear"))
            .choices("none", "bc7-linear")
            .help("BC7 for linear data textures; color textures retain hardware sRGB sampling");
    }

    AssetImportOptions getAssetImportOptions(const argparse::ArgumentParser& cli)
    {
        AssetImportOptions options;
        options.cacheDirectory       = cli.get<std::string>("--cache-dir");
        options.cache                = !cli.get<bool>("--no-cache");
        options.reimport             = cli.get<bool>("--reimport");
        options.textures.mipmaps     = !cli.get<bool>("--no-mipmaps");
        options.workers              = cli.get<uint32_t>("--import-jobs");
        options.textures.compression = cli.get<std::string>("--compression") == "none" ? TextureCompression::eNone :
                                                                                         TextureCompression::eBc7Linear;
        return options;
    }
} // namespace vultra
