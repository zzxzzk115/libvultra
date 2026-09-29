#pragma once

#include <vultra/core/base/command_line.hpp>
#include <vultra/function/asset/asset_pipeline.hpp>

namespace vultra
{
    void               addAssetImportOptions(argparse::ArgumentParser& cli);
    AssetImportOptions getAssetImportOptions(const argparse::ArgumentParser& cli);
} // namespace vultra
