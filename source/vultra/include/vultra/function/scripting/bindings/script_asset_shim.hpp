#pragma once

// Shim declarations for the Lua `Asset` namespace. The IR pipeline generates
// the sol2 registration (script_asset_binding.gen.cpp, including the value-type
// usertypes AssetHandle/TextAssetResult/AssetMemoryStats from VBIND_STRUCT and
// the AssetState enum); the bodies in script_asset_shim.cpp own the handle
// conversion and Result unwrapping.

#include "vultra/core/base/script_annotations.hpp"
#include "vultra/function/scripting/script_context.hpp"
#include "vultra/function/scripting/script_types.hpp"

#include <string>

namespace vultra
{
    struct VBIND_MODULE(name = Asset, service = assetService) AssetModule
    {
    };

    VBIND_FN(module = Asset, name = resolveUri, body = shim)
    std::string assetResolveUri(ScriptContext& ctx, const std::string& uri);

    VBIND_FN(module = Asset, name = loadText, body = shim)
    ScriptTextAssetResult assetLoadText(ScriptContext& ctx, const std::string& uri);

    VBIND_FN(module = Asset, name = loadMesh, body = shim)
    ScriptAssetHandle assetLoadMesh(ScriptContext& ctx, const std::string& uri);

    VBIND_FN(module = Asset, name = loadTexture, body = shim)
    ScriptAssetHandle assetLoadTexture(ScriptContext& ctx, const std::string& uri);

    VBIND_FN(module = Asset, name = loadGaussianSplat, body = shim)
    ScriptAssetHandle assetLoadGaussianSplat(ScriptContext& ctx, const std::string& uri);

    VBIND_FN(module = Asset, name = memoryStats, body = shim)
    ScriptAssetMemoryStats assetMemoryStats(ScriptContext& ctx);
} // namespace vultra
