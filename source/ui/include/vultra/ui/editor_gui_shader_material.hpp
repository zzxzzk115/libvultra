#pragma once

#include <vultra/assets/shader_asset.hpp>

namespace vultra
{
    class EditorGui;
    // Draw inside an active UI frame. Texture choices use the application's explicit asset map.
    bool drawShaderMaterialInspector(EditorGui&             gui,
                                     const ShaderAsset&     shader,
                                     MaterialInstance&      material,
                                     const ProjectManifest* assets = nullptr);
} // namespace vultra
