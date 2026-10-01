#pragma once

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/core/base/command_line.hpp>

namespace vultra
{
    void               addAssetImportOptions(argparse::ArgumentParser& cli);
    AssetImportOptions getAssetImportOptions(const argparse::ArgumentParser& cli);
} // namespace vultra
