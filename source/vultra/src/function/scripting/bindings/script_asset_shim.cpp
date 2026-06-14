#include "vultra/function/scripting/bindings/script_asset_shim.hpp"

#include "vultra/function/services/asset_service.hpp"

#include <utility>

namespace vultra
{
    namespace
    {
        template<typename Handle>
        ScriptAssetHandle toScriptAssetHandle(const Handle& handle)
        {
            return {.valid    = handle.valid(),
                    .ready    = handle.ready(),
                    .uuid     = handle.valid() ? handle.uuid().toString() : std::string {},
                    .state    = static_cast<int>(handle.state()),
                    .gpuIndex = handle.gpuIndex()};
        }
    } // namespace

    std::string assetResolveUri(ScriptContext& ctx, const std::string& uri)
    {
        return ctx.assetService ? ctx.assetService->resolveUri(uri) : std::string {};
    }

    ScriptTextAssetResult assetLoadText(ScriptContext& ctx, const std::string& uri)
    {
        if (!ctx.assetService)
            return ScriptTextAssetResult {.ok = false, .error = "Asset service unavailable"};

        auto result = ctx.assetService->loadTextAssetSync(uri);
        if (!result)
            return ScriptTextAssetResult {.ok = false, .error = std::move(result).error()};

        return ScriptTextAssetResult {.ok = true, .text = std::move(result).value()};
    }

    ScriptAssetHandle assetLoadMesh(ScriptContext& ctx, const std::string& uri)
    {
        return ctx.assetService ? toScriptAssetHandle(ctx.assetService->loadMeshSync(uri)) : ScriptAssetHandle {};
    }

    ScriptAssetHandle assetLoadTexture(ScriptContext& ctx, const std::string& uri)
    {
        return ctx.assetService ? toScriptAssetHandle(ctx.assetService->loadTextureSync(uri)) : ScriptAssetHandle {};
    }

    ScriptAssetHandle assetLoadGaussianSplat(ScriptContext& ctx, const std::string& uri)
    {
        return ctx.assetService ? toScriptAssetHandle(ctx.assetService->loadGaussianSplatSync(uri)) :
                                  ScriptAssetHandle {};
    }

    ScriptAssetMemoryStats assetMemoryStats(ScriptContext& ctx)
    {
        if (!ctx.assetService)
            return ScriptAssetMemoryStats {};

        const auto stats = ctx.assetService->memoryStats();
        return ScriptAssetMemoryStats {.cpuCacheBytes = stats.cpuCacheBytes};
    }
} // namespace vultra
