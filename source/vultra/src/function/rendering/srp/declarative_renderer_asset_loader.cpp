// Lua render-pipeline asset loading for DeclarativeRenderer: .vrp.lua pipeline /
// feature / scripted-pass / shader-library parsing. The per-frame runtime lives in
// declarative_renderer.cpp; only asset (re)loading is defined in this TU.

#include "vultra/function/rendering/srp/declarative_renderer.hpp"

#include "declarative_lua_utils.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/function/rendering/srp/builtin/render_graph_backbuffer.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/plugin_service.hpp"
#include "vultra/function/services/render_service.hpp"
#include "vultra/function/services/shader_service.hpp"

#include <vbase/core/hash.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace vultra
{
    namespace
    {
        [[nodiscard]] std::string rendererKeyFromRenderGraphUri(std::string_view uri)
        {
            auto filename = std::filesystem::path(std::string(uri)).filename().generic_string();
            constexpr std::array<std::string_view, 2> suffixes {".vrg.json", ".vrp.lua"};
            for (const auto suffix : suffixes)
            {
                if (filename.ends_with(suffix))
                {
                    filename.resize(filename.size() - suffix.size());
                    break;
                }
            }
            if (filename.empty())
                filename = "custom";
            return normalizeRenderGraphId(std::move(filename));
        }

        [[nodiscard]] std::string uriFromLogicalPath(std::string_view logicalPath)
        {
            return logicalPath.starts_with("res://") ? std::string(logicalPath) : "res://" + std::string(logicalPath);
        }

        [[nodiscard]] bool isImportedAssetPath(std::string_view logicalPath)
        {
            return logicalPath == "imported" || logicalPath.starts_with("imported/");
        }

        [[nodiscard]] sol::state makeAssetLuaState()
        {
            sol::state lua;
            lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math);
            lua.set_function("RenderPipelineAsset", [](sol::table t) { return t; });
            lua.set_function("RenderFeature", [](sol::table t) { return t; });
            lua.set_function("RenderGraphPass", [](sol::table t) { return t; });
            lua.set_function("ShaderLibrary", [](sol::table t) { return t; });
            lua.set_function("ShadingModel", [](sol::table t) { return t; });
            return lua;
        }
    } // namespace

    bool DeclarativeRenderer::loadPipelineAsset()
    {
        auto* services     = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService)
            return false;

        auto text = assetService->loadTextAssetSync(m_PipelineUri);
        if (!text)
        {
            VULTRA_CORE_ERROR(
                "[DeclarativeRenderer] Failed to load SRP asset '{}': {}", m_PipelineUri, std::move(text).error());
            return false;
        }

        if (m_PipelineUri.ends_with(".vrg.json"))
        {
            try
            {
                const bool builtinGraph = m_PipelineUri.starts_with("builtin://");
                const auto json         = nlohmann::json::parse(text.value());
                static_cast<void>(vrendergraph::loadRenderGraph(json));
                auto feature        = Feature {};
                feature.name        = m_PipelineUri;
                feature.renderGraph = m_PipelineUri;
                m_Asset.rendererKey = m_RendererKeyOverride.empty() ? rendererKeyFromRenderGraphUri(m_PipelineUri) :
                                                                      m_RendererKeyOverride;
                if (!builtinGraph)
                {
                    m_Asset.shaderLibraries.try_emplace("project", "res://shaders/project.vshaderlib.lua");
                    loadScriptedPasses();
                }
                m_Asset.features.push_back(std::move(feature));
                return true;
            }
            catch (const std::exception& e)
            {
                VULTRA_CORE_ERROR(
                    "[DeclarativeRenderer] Failed to parse render graph pipeline '{}': {}", m_PipelineUri, e.what());
                return false;
            }
        }

        auto lua    = makeAssetLuaState();
        auto result = lua.safe_script(text.value(), &sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            VULTRA_CORE_ERROR(
                "[DeclarativeRenderer] Failed to parse render pipeline '{}': {}", m_PipelineUri, err.what());
            return false;
        }

        sol::object obj = result;
        if (!obj.is<sol::table>() || !parsePipelineTable(obj.as<sol::table>(), m_Asset))
        {
            VULTRA_CORE_ERROR("[DeclarativeRenderer] Render pipeline '{}' did not return a valid RenderPipelineAsset",
                              m_PipelineUri);
            return false;
        }

        if (!m_RendererKeyOverride.empty())
            m_Asset.rendererKey = m_RendererKeyOverride;
        m_Asset.shaderLibraries.try_emplace("project", "res://shaders/project.vshaderlib.lua");
        m_ShadingModelRegistry.clear();
        for (const auto& model : m_Asset.shadingModels)
        {
            const uint32_t code = m_ShadingModelRegistry.registerModel(model);
            VULTRA_CORE_INFO("[DeclarativeRenderer] Registered custom shading model '{}' as code {}", model.name, code);
        }
        loadScriptedPasses();
        return true;
    }

    bool DeclarativeRenderer::loadFeatureAsset(std::string_view uri, Feature& outFeature)
    {
        auto* services     = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService)
            return false;

        auto text = assetService->loadTextAssetSync(uri);
        if (!text)
            return false;

        auto lua    = makeAssetLuaState();
        auto result = lua.safe_script(text.value(), &sol::script_pass_on_error);
        if (!result.valid())
            return false;

        sol::object obj = result;
        return obj.is<sol::table>() && parseFeatureTable(obj.as<sol::table>(), outFeature);
    }

    bool DeclarativeRenderer::parsePipelineTable(sol::table table, PipelineAsset& outAsset)
    {
        outAsset.rendererKey = getString(table, "rendererKey", "custom");

        sol::object libsObj = table["shaderLibraries"];
        if (libsObj.is<sol::table>())
        {
            sol::table libs = libsObj.as<sol::table>();
            for (const auto& [key, value] : libs)
            {
                if (key.is<std::string>() && value.is<std::string>())
                    outAsset.shaderLibraries[key.as<std::string>()] = value.as<std::string>();
            }
        }

        // Custom shading models: `shadingModels = { ShadingModel{ ... }, ... }`.
        sol::object modelsObj = table["shadingModels"];
        if (modelsObj.is<sol::table>())
        {
            sol::table models = modelsObj.as<sol::table>();
            for (const auto& [_, value] : models)
            {
                static_cast<void>(_);
                material::ShadingModelDesc model;
                if (value.is<sol::table>() && parseShadingModelTable(value.as<sol::table>(), model))
                    outAsset.shadingModels.push_back(std::move(model));
            }
        }

        sol::object featuresObj = table["features"];
        if (!featuresObj.is<sol::table>())
            return true;

        sol::table features = featuresObj.as<sol::table>();
        for (const auto& [_, value] : features)
        {
            static_cast<void>(_);
            Feature feature;
            if (value.is<std::string>())
            {
                const auto featureRef = value.as<std::string>();
                if (featureRef.starts_with("res://") || featureRef.find(".lua") != std::string::npos)
                {
                    if (featureRef.find(".vrg.json") != std::string::npos)
                    {
                        feature.name        = featureRef;
                        feature.renderGraph = featureRef;
                        outAsset.features.push_back(std::move(feature));
                    }
                    else if (loadFeatureAsset(featureRef, feature))
                        outAsset.features.push_back(std::move(feature));
                }
                else
                {
                    feature.name    = featureRef;
                    feature.builtin = featureRef;
                    outAsset.features.push_back(std::move(feature));
                }
            }
            else if (value.is<sol::table>() && parseFeatureTable(value.as<sol::table>(), feature))
            {
                outAsset.features.push_back(std::move(feature));
            }
        }
        return true;
    }

    bool DeclarativeRenderer::parseShadingModelTable(sol::table table, material::ShadingModelDesc& outModel)
    {
        outModel.name = getString(table, "name");
        if (outModel.name.empty())
            return false;
        outModel.bxdfLibrary    = getString(table, "bxdfLibrary", "project");
        outModel.bxdfArtifact   = getString(table, "bxdfArtifact");
        outModel.bxdfFunction   = getString(table, "bxdfFunction");
        outModel.extraParamSize = static_cast<uint32_t>(std::max(0, getInt(table, "extraParamSize", 0)));
        // defaultExtraParams (a typed param block) is left empty here; it is wired with
        // the ShadingModelParamsBuffer when the forward custom-BXDF shading path lands.
        return true;
    }

    bool DeclarativeRenderer::parseFeatureTable(sol::table table, Feature& outFeature)
    {
        outFeature.name    = getString(table, "name", "LuaRenderFeature");
        outFeature.builtin = getString(table, "builtin");
        if (outFeature.builtin.empty())
            outFeature.builtin = getString(table, "useBuiltin");

        sol::object passesObj = table["passes"];
        if (!passesObj.is<sol::table>())
            return true;

        sol::table passes = passesObj.as<sol::table>();
        for (const auto& [_, passObj] : passes)
        {
            static_cast<void>(_);
            if (!passObj.is<sol::table>())
                continue;

            sol::table passTable = passObj.as<sol::table>();
            const auto type      = getString(passTable, "type", "fullscreen");
            if (type != "fullscreen")
                continue;

            FullscreenPass pass;
            pass.name             = getString(passTable, "name", outFeature.name + "::Fullscreen");
            sol::object shaderObj = passTable["shader"];
            if (!shaderObj.is<sol::table>())
                continue;

            sol::table shaderTable = shaderObj.as<sol::table>();
            pass.shader.library    = getString(shaderTable, "library", "project");
            pass.shader.vertex     = getString(shaderTable, "vertex");
            pass.shader.fragment   = getString(shaderTable, "fragment");
            const auto defaultVertexLibrary =
                pass.shader.vertex == "fullscreen_triangle.vert" ? std::string {"builtin"} : pass.shader.library;
            pass.shader.vertexLibrary =
                getString(shaderTable, "vertexLibrary", getString(shaderTable, "vertex_library", defaultVertexLibrary));
            pass.shader.fragmentLibrary = getString(
                shaderTable, "fragmentLibrary", getString(shaderTable, "fragment_library", pass.shader.library));
            pass.input  = getString(passTable, "input", pass.input);
            pass.output = getString(passTable, "output", pass.output);
            if (pass.shader.vertex.empty() || pass.shader.fragment.empty())
                continue;

            outFeature.fullscreenPasses.push_back(std::move(pass));
        }
        return true;
    }

    void DeclarativeRenderer::loadScriptedPasses()
    {
        auto* services     = getServices();
        auto* assetService = services ? services->tryGet<IAssetService>() : nullptr;
        if (!assetService)
            return;
        auto* shaderService = services ? services->tryGet<IShaderService>() : nullptr;

        // Pass-definition diagnostics are rebuilt on every (re)load. Shader-
        // resolution diagnostics are re-published per frame from each pass's setup.
        if (shaderService)
            shaderService->clearRenderPassDiagnostics();

        std::unordered_set<std::string> loadedLogicalPaths;
        const auto parsePassText = [this, shaderService](
                                       std::string_view label, std::string_view text, std::string sourcePath) {
            // Report (and log) an invalid pass definition against its source .lua so
            // the code editor can mark it. label is for the log, sourcePath keys the UI.
            const auto reportDefinitionError = [&](std::string message) {
                VULTRA_CORE_ERROR("[DeclarativeRenderer] Invalid render pass '{}': {}", label, message);
                if (shaderService && !sourcePath.empty())
                    shaderService->setRenderPassDiagnostics(sourcePath,
                                                            {AssetDiagnostic {sourcePath, 0, 0, std::move(message)}});
            };

            if (text.find("RenderGraphPass") == std::string_view::npos)
                return;

            // Run in the persistent render-script state inside a fresh environment:
            // a scripted pass returns `setup`/`execute` sol::functions that must
            // outlive parsing (they execute every frame), and per-file environments
            // keep each pass file's globals from leaking into the next.
            auto&            lua = renderScriptState();
            sol::environment env(lua, sol::create, lua.globals());
            auto             result = lua.safe_script(std::string(text), env, &sol::script_pass_on_error);
            if (!result.valid())
            {
                const sol::error err = result;
                reportDefinitionError(std::string {"Lua error: "} + err.what());
                return;
            }

            sol::object obj = result;
            if (!obj.is<sol::table>())
            {
                reportDefinitionError("script must return a RenderGraphPass { ... } table.");
                return;
            }

            sol::table table = obj.as<sol::table>();
            if (table["setup"].get_type() == sol::type::function && table["execute"].get_type() == sol::type::function)
            {
                ScriptedPassDef def;
                if (parseScriptedPassTable(table, def))
                {
                    def.sourcePath = sourcePath;
                    m_Asset.scriptedPasses.push_back(std::move(def));
                }
                else
                {
                    reportDefinitionError("missing a required 'type' (or 'name') field.");
                }
                return;
            }

            reportDefinitionError("missing setup/execute functions. See doc/scripted_render_passes.md.");
        };

        const auto assetRoot = std::filesystem::path(assetService->resolveUri("res://")).lexically_normal();
        const bool shouldScanPhysicalAssetRoot =
            !assetRoot.empty() && assetRoot != assetRoot.root_path() && assetRoot != assetRoot.root_name();
        std::error_code ec;
        if (shouldScanPhysicalAssetRoot && std::filesystem::is_directory(assetRoot, ec))
        {
            std::vector<std::filesystem::path> files;
            for (auto it = std::filesystem::recursive_directory_iterator(
                     assetRoot, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator {};
                 it.increment(ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }
                const auto& entry = *it;
                if (!entry.is_regular_file(ec) || entry.path().extension() != ".lua")
                {
                    ec.clear();
                    continue;
                }
                std::error_code relEc;
                const auto      rel = std::filesystem::relative(entry.path(), assetRoot, relEc);
                if (relEc || rel.empty() || isImportedAssetPath(rel.generic_string()))
                    continue;
                files.push_back(entry.path().lexically_normal());
            }
            std::sort(files.begin(), files.end());

            for (const auto& file : files)
            {
                std::ifstream stream(file);
                if (!stream.is_open())
                    continue;

                std::stringstream buffer;
                buffer << stream.rdbuf();

                std::error_code relEc;
                const auto      rel = std::filesystem::relative(file, assetRoot, relEc);
                std::string     logical;
                if (!relEc && !rel.empty())
                {
                    logical = rel.generic_string();
                    loadedLogicalPaths.insert(logical);
                }

                parsePassText(file.generic_string(), buffer.str(), logical);
            }
        }

        // Active plugins ship render passes too (`<plugin>/render/passes/*.lua`). Managed plugins
        // live outside the asset root (.vultra/plugins/<id>/<version>), so scan their content
        // roots explicitly; project passes were parsed first, so a project pass of the same type
        // still wins at registration.
        if (auto* pluginService = services->tryGet<IPluginService>())
        {
            for (const auto& contentRoot : pluginService->contentRoots())
            {
                const auto      passesDir = contentRoot.directory / "render" / "passes";
                std::error_code ec;
                if (!std::filesystem::is_directory(passesDir, ec))
                    continue;

                std::vector<std::filesystem::path> files;
                for (auto it = std::filesystem::recursive_directory_iterator(
                         passesDir, std::filesystem::directory_options::skip_permission_denied, ec);
                     it != std::filesystem::recursive_directory_iterator {};
                     it.increment(ec))
                {
                    if (ec)
                    {
                        ec.clear();
                        continue;
                    }
                    if (it->is_regular_file(ec) && it->path().extension() == ".lua")
                        files.push_back(it->path().lexically_normal());
                    ec.clear();
                }
                std::sort(files.begin(), files.end());

                std::size_t parsed = 0;
                for (const auto& file : files)
                {
                    std::ifstream stream(file);
                    if (!stream.is_open())
                        continue;
                    std::stringstream buffer;
                    buffer << stream.rdbuf();

                    std::error_code relEc;
                    const auto      rel = std::filesystem::relative(file, passesDir, relEc);
                    const auto      uri = contentRoot.uri + "/render/passes/" +
                                     (relEc || rel.empty() ? file.filename().generic_string() : rel.generic_string());
                    parsePassText(uri, buffer.str(), uri);
                    ++parsed;
                }
                if (parsed > 0)
                    VULTRA_CORE_INFO("[DeclarativeRenderer] Plugin '{}': scanned {} render pass script(s) ({}).",
                                     contentRoot.id,
                                     parsed,
                                     contentRoot.uri);
            }
        }

        std::vector<std::string> registryPassUris;
        for (const auto& [_, entry] : assetService->registry().getRegistry())
        {
            static_cast<void>(_);
            if (entry.type != vasset::VAssetType::eScriptLua || entry.sourcePath.empty())
                continue;

            const auto logicalPath = std::filesystem::path(entry.sourcePath).generic_string();
            if (!logicalPath.ends_with(".lua") || isImportedAssetPath(logicalPath) ||
                loadedLogicalPaths.contains(logicalPath))
            {
                continue;
            }

            registryPassUris.push_back(uriFromLogicalPath(logicalPath));
        }
        std::sort(registryPassUris.begin(), registryPassUris.end());
        registryPassUris.erase(std::unique(registryPassUris.begin(), registryPassUris.end()), registryPassUris.end());

        for (const auto& uri : registryPassUris)
        {
            auto text = assetService->loadTextAssetSync(uri);
            if (!text)
            {
                VULTRA_CORE_ERROR(
                    "[DeclarativeRenderer] Failed to load project graph pass '{}': {}", uri, std::move(text).error());
                continue;
            }
            parsePassText(uri, text.value(), uri);
        }
    }

    bool DeclarativeRenderer::loadShaderLibraries()
    {
        auto* services      = getServices();
        auto* shaderService = services ? services->tryGet<IShaderService>() : nullptr;
        if (!shaderService)
            return false;

        // Plugin-shipped shader libraries: precompiled `.vshlib` artifacts under
        // `<plugin>/shaders/` register automatically -- as `<plugin-id>` (the first one, so a
        // single-library plugin is addressable by its id) and as `<plugin-id>/<stem>`. Plugins
        // ship compiled artifacts like they ship prebuilt native libraries; shader *sources*
        // are not compiled from plugin folders. Graph-declared names take precedence.
        if (auto* pluginService = services->tryGet<IPluginService>())
        {
            for (const auto& contentRoot : pluginService->contentRoots())
            {
                const auto      shadersDir = contentRoot.directory / "shaders";
                std::error_code ec;
                if (!std::filesystem::is_directory(shadersDir, ec))
                    continue;

                std::vector<std::filesystem::path> artifacts;
                for (auto it = std::filesystem::recursive_directory_iterator(
                         shadersDir, std::filesystem::directory_options::skip_permission_denied, ec);
                     it != std::filesystem::recursive_directory_iterator {};
                     it.increment(ec))
                {
                    if (ec)
                    {
                        ec.clear();
                        continue;
                    }
                    if (it->is_regular_file(ec) && it->path().extension() == ".vshlib")
                        artifacts.push_back(it->path().lexically_normal());
                    ec.clear();
                }
                std::sort(artifacts.begin(), artifacts.end());

                for (std::size_t i = 0; i < artifacts.size(); ++i)
                {
                    std::error_code relEc;
                    const auto      rel = std::filesystem::relative(artifacts[i], shadersDir, relEc);
                    const auto      uri = contentRoot.uri + "/shaders/" +
                                     (relEc || rel.empty() ? artifacts[i].filename().generic_string() :
                                                             rel.generic_string());
                    if (i == 0)
                        m_Asset.shaderLibraries.try_emplace(contentRoot.id, uri);
                    m_Asset.shaderLibraries.try_emplace(
                        contentRoot.id + "/" + artifacts[i].stem().generic_string(), uri);
                }
                if (!artifacts.empty())
                    VULTRA_CORE_INFO("[DeclarativeRenderer] Plugin '{}': registered {} shader librar{} ({}).",
                                     contentRoot.id,
                                     artifacts.size(),
                                     artifacts.size() == 1 ? "y" : "ies",
                                     contentRoot.uri);
            }
        }

        for (const auto& [name, uri] : m_Asset.shaderLibraries)
        {
            if (!shaderService->reloadProjectLibrary(uri))
            {
                // The project shader library is an optional override: when it isn't present the
                // builtin shaders are used. A genuinely malformed library is still error-logged by
                // the shader system, so treat a load miss here as non-fatal.
                VULTRA_CORE_TRACE(
                    "[DeclarativeRenderer] Shader library '{}' ('{}') not loaded; using builtin shaders.", name, uri);
            }
        }
        return true;
    }
} // namespace vultra
