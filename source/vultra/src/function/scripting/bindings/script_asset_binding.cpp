#include "vultra/function/scripting/bindings/script_asset_binding.hpp"

#include "vultra/function/asset/asset_state.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/scripting/bindings/script_binding_common.hpp"
#include "vultra/function/scripting/script_types.hpp"

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

    void registerScriptAssetBindings(sol::state& lua, ScriptContext& ctx)
    {
        lua.new_usertype<ScriptAssetHandle>("AssetHandle",
                                            "valid",
                                            &ScriptAssetHandle::valid,
                                            "ready",
                                            &ScriptAssetHandle::ready,
                                            "uuid",
                                            &ScriptAssetHandle::uuid,
                                            "state",
                                            &ScriptAssetHandle::state,
                                            "gpuIndex",
                                            &ScriptAssetHandle::gpuIndex);

        lua.new_usertype<ScriptTextAssetResult>("TextAssetResult",
                                                "ok",
                                                &ScriptTextAssetResult::ok,
                                                "text",
                                                &ScriptTextAssetResult::text,
                                                "error",
                                                &ScriptTextAssetResult::error);

        lua.new_usertype<ScriptAssetMemoryStats>("AssetMemoryStats",
                                                 "cpuCacheBytes",
                                                 &ScriptAssetMemoryStats::cpuCacheBytes);

        auto asset = script_binding::getOrCreateTable(lua, "Asset");

        asset.set_function("resolveUri", [&ctx](const std::string& uri) {
            return ctx.assetService ? ctx.assetService->resolveUri(uri) : std::string {};
        });

        asset.set_function("loadText", [&ctx](const std::string& uri) {
            if (!ctx.assetService)
                return ScriptTextAssetResult {.ok = false, .error = "Asset service unavailable"};

            auto result = ctx.assetService->loadTextAssetSync(uri);
            if (!result)
                return ScriptTextAssetResult {.ok = false, .error = std::move(result).error()};

            return ScriptTextAssetResult {.ok = true, .text = std::move(result).value()};
        });

        asset.set_function("loadMesh", [&ctx](const std::string& uri) {
            return ctx.assetService ? toScriptAssetHandle(ctx.assetService->loadMeshSync(uri)) : ScriptAssetHandle {};
        });

        asset.set_function("loadTexture", [&ctx](const std::string& uri) {
            return ctx.assetService ? toScriptAssetHandle(ctx.assetService->loadTextureSync(uri)) : ScriptAssetHandle {};
        });

        asset.set_function("loadGaussianSplat", [&ctx](const std::string& uri) {
            return ctx.assetService ? toScriptAssetHandle(ctx.assetService->loadGaussianSplatSync(uri))
                                    : ScriptAssetHandle {};
        });

        asset.set_function("memoryStats", [&ctx]() {
            if (!ctx.assetService)
                return ScriptAssetMemoryStats {};

            const auto stats = ctx.assetService->memoryStats();
            return ScriptAssetMemoryStats {.cpuCacheBytes = stats.cpuCacheBytes};
        });

        script_binding::bindEnumTable<AssetState>(lua, "AssetState");
    }
} // namespace vultra
