#pragma once

#include <vultra/assets/asset_entry.hpp>
#include <vultra/assets/shader_asset.hpp>

#include <span>

namespace vultra
{
    class EditorGui;
    // Draw inside an active UI frame. Texture choices use the application's explicit asset map.
    bool drawShaderMaterialInspector(EditorGui&                  gui,
                                     const ShaderAsset&          shader,
                                     MaterialInstance&           material,
                                     std::span<const AssetEntry> assets = {});
} // namespace vultra
