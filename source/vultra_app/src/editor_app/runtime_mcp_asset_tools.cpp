#include "editor_app/runtime_mcp_server.hpp"

#include "editor_app/vultra_package.hpp"

#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_service.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace vultra_app
{
    namespace
    {
        std::string lowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        }

        nlohmann::json toolJson(nlohmann::json payload)
        {
            return {{"content", nlohmann::json::array({{{"type", "text"}, {"text", payload.dump(2)}}})}};
        }

        nlohmann::json toolError(std::string message)
        {
            return {{"isError", true},
                    {"content",
                     nlohmann::json::array({{{"type", "text"},
                                              {"text", nlohmann::json({{"ok", false}, {"error", std::move(message)}}).dump(2)}}})}};
        }

        std::filesystem::path projectAssetRoot(const EditorContext& ctx)
        {
            if (ctx.state.currentProject.empty())
                return {};
            return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        }

        bool pathInside(const std::filesystem::path& child, const std::filesystem::path& root)
        {
            std::error_code ec;
            const auto      absChild = std::filesystem::weakly_canonical(child, ec);
            if (ec)
                return false;
            const auto absRoot = std::filesystem::weakly_canonical(root, ec);
            if (ec)
                return false;

            auto childIt = absChild.begin();
            auto rootIt  = absRoot.begin();
            for (; rootIt != absRoot.end(); ++rootIt, ++childIt)
            {
                if (childIt == absChild.end() || *childIt != *rootIt)
                    return false;
            }
            return true;
        }

        std::optional<std::filesystem::path> resolveProjectAssetPath(const EditorContext& ctx,
                                                                     std::string_view     uriOrPath,
                                                                     std::string*         error = nullptr)
        {
            const auto root = projectAssetRoot(ctx);
            if (root.empty())
            {
                if (error)
                    *error = "no project is loaded";
                return std::nullopt;
            }

            std::filesystem::path path;
            constexpr std::string_view resPrefix {"res://"};
            if (uriOrPath.starts_with(resPrefix))
                path = root / std::string(uriOrPath.substr(resPrefix.size()));
            else
                path = std::filesystem::path(std::string(uriOrPath));
            path = path.lexically_normal();
            if (!path.is_absolute())
                path = (root / path).lexically_normal();

            if (!pathInside(path, root))
            {
                if (error)
                    *error = "path is outside the current project asset root";
                return std::nullopt;
            }
            return path;
        }

        std::string assetUriForPath(const EditorContext& ctx, const std::filesystem::path& path)
        {
            const auto root = projectAssetRoot(ctx);
            std::error_code ec;
            auto rel = std::filesystem::relative(path.lexically_normal(), root, ec);
            if (ec)
                rel = path.filename();
            auto uri = rel.generic_string();
            if (!uri.starts_with("res://"))
                uri = "res://" + uri;
            return uri;
        }

        nlohmann::json importDiagnosticsJson(const std::vector<vultra::AssetDiagnostic>& diagnostics)
        {
            nlohmann::json out = nlohmann::json::array();
            for (const auto& diagnostic : diagnostics)
            {
                out.push_back({{"path", diagnostic.path},
                               {"line", diagnostic.line},
                               {"column", diagnostic.column},
                               {"message", diagnostic.message}});
            }
            return out;
        }

        bool isWebUrl(const std::string& url)
        {
            const auto lower = lowerAscii(url);
            return lower.starts_with("http://") || lower.starts_with("https://");
        }

        bool shellSafe(const std::string& text)
        {
            return text.find_first_of("\"`$%!\\\r\n") == std::string::npos;
        }

        std::string shellQuote(const std::filesystem::path& path)
        {
            return "\"" + path.generic_string() + "\"";
        }

        std::string shellQuoteText(const std::string& text)
        {
            return "\"" + text + "\"";
        }

        std::string sanitizeFilename(std::string filename)
        {
            for (auto& ch : filename)
            {
                const auto uch = static_cast<unsigned char>(ch);
                if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
                    ch == '>' || ch == '|' || std::iscntrl(uch))
                    ch = '_';
            }
            if (filename.empty() || filename == "." || filename == "..")
                filename = "downloaded_asset";
            return filename;
        }

        std::string filenameFromUrl(const std::string& url)
        {
            auto end = url.find_first_of("?#");
            if (end == std::string::npos)
                end = url.size();
            const auto slash = url.find_last_of('/', end == 0 ? 0 : end - 1);
            if (slash == std::string::npos || slash + 1 >= end)
                return "downloaded_asset";
            return sanitizeFilename(url.substr(slash + 1, end - slash - 1));
        }
    } // namespace

    nlohmann::json RuntimeMcpServer::handleAssetTool(std::string_view name,
                                                     const nlohmann::json& args,
                                                     EditorContext& ctx)
    {
        if (name == "vultra.assets.list")
        {
            auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            if (!assetService)
                return toolError("asset service is unavailable");

            const auto typeFilter  = lowerAscii(args.value("type", std::string {}));
            const auto queryFilter = lowerAscii(args.value("query", std::string {}));
            const int  limit       = std::clamp(args.value("limit", 512), 1, 8192);

            nlohmann::json assets = nlohmann::json::array();
            int            matched = 0;
            for (const auto& [uuid, entry] : assetService->registry().getRegistry())
            {
                const auto typeName = vasset::toString(entry.type);
                if (!typeFilter.empty() && lowerAscii(typeName) != typeFilter)
                    continue;
                const auto haystack = lowerAscii(uuid + " " + entry.sourcePath + " " + entry.importedPath + " " + typeName);
                if (!queryFilter.empty() && haystack.find(queryFilter) == std::string::npos)
                    continue;

                ++matched;
                if (static_cast<int>(assets.size()) >= limit)
                    continue;
                assets.push_back({{"uuid", uuid},
                                  {"type", typeName},
                                  {"sourcePath", entry.sourcePath},
                                  {"sourceUri", entry.sourcePath.empty() ? std::string {} : "res://" + entry.sourcePath},
                                  {"importedPath", entry.importedPath},
                                  {"importedUri", entry.importedPath.empty() ? std::string {} : "res://" + entry.importedPath}});
            }
            return toolJson({{"ok", true},
                             {"assetRoot", assetService->registry().getAssetRootPath()},
                             {"matched", matched},
                             {"returned", assets.size()},
                             {"assets", std::move(assets)}});
        }

        if (name == "vultra.assets.read")
        {
            const auto uri = args.value("uri", std::string {});
            if (uri.empty())
                return toolError("assets.read requires uri");

            auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            if (assetService)
            {
                auto text = assetService->loadTextAssetSync(uri);
                if (text)
                    return toolJson({{"ok", true}, {"uri", uri}, {"text", text.value()}, {"source", "asset_service"}});
            }

            std::string error;
            auto        path = resolveProjectAssetPath(ctx, uri, &error);
            if (!path)
                return toolError(error);
            std::ifstream in(*path, std::ios::binary);
            if (!in)
                return toolError("failed to open asset: " + path->generic_string());
            std::ostringstream text;
            text << in.rdbuf();
            return toolJson({{"ok", true}, {"uri", uri}, {"path", path->generic_string()}, {"text", text.str()}, {"source", "filesystem"}});
        }

        if (name == "vultra.assets.write")
        {
            const auto uri = args.value("uri", std::string {});
            if (uri.empty() || !args.contains("text") || !args["text"].is_string())
                return toolError("assets.write requires uri and string text");
            if (!args.value("allowWrite", false))
                return toolError("assets.write requires allowWrite=true");

            std::string error;
            auto        path = resolveProjectAssetPath(ctx, uri, &error);
            if (!path)
                return toolError(error);

            std::error_code ec;
            std::filesystem::create_directories(path->parent_path(), ec);
            if (ec)
                return toolError("failed to create parent directory: " + ec.message());
            std::ofstream out(*path, std::ios::binary | std::ios::trunc);
            if (!out)
                return toolError("failed to open asset for writing: " + path->generic_string());
            const auto text = args["text"].get<std::string>();
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!out)
                return toolError("failed to write asset: " + path->generic_string());

            ++ctx.state.assetFileGeneration;
            bool imported = false;
            nlohmann::json diagnostics = nlohmann::json::array();
            if (args.value("reimport", true))
            {
                if (auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
                {
                    imported    = assetService->reimportAsset(assetUriForPath(ctx, *path), true);
                    diagnostics = importDiagnosticsJson(assetService->lastImportDiagnostics());
                }
            }
            return toolJson({{"ok", true},
                             {"uri", assetUriForPath(ctx, *path)},
                             {"path", path->generic_string()},
                             {"bytesWritten", text.size()},
                             {"reimported", imported},
                             {"diagnostics", std::move(diagnostics)}});
        }

        if (name == "vultra.assets.import")
        {
            auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            if (!assetService)
                return toolError("asset service is unavailable");

            auto uri = args.value("uri", std::string {});
            if (uri.empty())
            {
                const auto pathArg = args.value("path", std::string {});
                if (pathArg.empty())
                    return toolError("assets.import requires uri or path");
                std::string error;
                auto        path = resolveProjectAssetPath(ctx, pathArg, &error);
                if (!path)
                    return toolError(error);
                uri = assetUriForPath(ctx, *path);
            }
            const bool ok = assetService->reimportAsset(uri, args.value("force", true));
            return toolJson({{"ok", ok}, {"uri", uri}, {"diagnostics", importDiagnosticsJson(assetService->lastImportDiagnostics())}});
        }

        if (name == "vultra.assets.import_from_web")
        {
            const auto url = args.value("url", std::string {});
            if (url.empty() || !isWebUrl(url))
                return toolError("assets.import_from_web requires http:// or https:// url");
            if (!shellSafe(url))
                return toolError("assets.import_from_web url contains unsupported shell characters");

            const auto targetUri = args.value("targetUri", std::string {});
            const auto targetDirectory = args.value("targetDirectory", std::string {"res://downloads"});
            std::string error;
            std::optional<std::filesystem::path> targetPath;
            if (!targetUri.empty())
                targetPath = resolveProjectAssetPath(ctx, targetUri, &error);
            else
            {
                auto directory = resolveProjectAssetPath(ctx, targetDirectory, &error);
                if (directory)
                    targetPath = (*directory / filenameFromUrl(url)).lexically_normal();
            }
            if (!targetPath)
                return toolError(error.empty() ? "failed to resolve web import target" : error);

            const auto root = projectAssetRoot(ctx);
            if (!pathInside(*targetPath, root))
                return toolError("target path is outside the current project asset root");
            if (!shellSafe(targetPath->generic_string()))
                return toolError("target path contains unsupported shell characters");

            const auto maxBytes = static_cast<uint64_t>(std::max(args.value("maxBytes", 256 * 1024 * 1024), 1));
            const auto timeoutSeconds = std::clamp(args.value("timeoutSeconds", 60), 1, 3600);
            const bool allowOverwrite = args.value("allowOverwrite", false);
            const bool reimport = args.value("reimport", true);

            std::error_code ec;
            if (std::filesystem::exists(*targetPath, ec) && !allowOverwrite)
                return toolError("target already exists; pass allowOverwrite=true: " + targetPath->generic_string());
            std::filesystem::create_directories(targetPath->parent_path(), ec);
            if (ec)
                return toolError("failed to create target directory: " + ec.message());

            auto tempDirectory = std::filesystem::path {".vultra"} / "mcp" / "web_downloads";
            std::filesystem::create_directories(tempDirectory, ec);
            if (ec)
                return toolError("failed to create temporary download directory: " + ec.message());
            auto tempPath = tempDirectory / (sanitizeFilename(targetPath->filename().generic_string()) + ".download");
            if (!shellSafe(tempPath.generic_string()))
                return toolError("temporary target path contains unsupported shell characters");
            std::filesystem::remove(tempPath, ec);

            std::ostringstream cmd;
            cmd << "curl -L --fail --silent --show-error"
                << " --max-time " << timeoutSeconds
                << " --max-filesize " << maxBytes
                << " --output " << shellQuote(tempPath)
                << " " << shellQuoteText(url);
            const int exitCode = std::system(cmd.str().c_str());
            if (exitCode != 0)
            {
                std::filesystem::remove(tempPath, ec);
                return toolError("curl failed while importing web asset, exit code: " + std::to_string(exitCode));
            }
            const auto bytesDownloaded = std::filesystem::file_size(tempPath, ec);
            if (ec)
            {
                std::filesystem::remove(tempPath, ec);
                return toolError("failed to stat downloaded asset: " + ec.message());
            }
            if (bytesDownloaded > maxBytes)
            {
                std::filesystem::remove(tempPath, ec);
                return toolError("downloaded asset exceeded maxBytes");
            }

            std::filesystem::rename(tempPath, *targetPath, ec);
            if (ec)
            {
                std::filesystem::copy_file(tempPath, *targetPath, std::filesystem::copy_options::overwrite_existing, ec);
                std::filesystem::remove(tempPath, ec);
                if (ec)
                    return toolError("failed to move downloaded asset into place: " + ec.message());
            }
            std::filesystem::remove(tempPath, ec);

            ++ctx.state.assetFileGeneration;
            const auto uri = assetUriForPath(ctx, *targetPath);
            bool imported = false;
            nlohmann::json diagnostics = nlohmann::json::array();
            if (reimport)
            {
                if (auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
                {
                    imported = assetService->reimportAsset(uri, args.value("force", true));
                    diagnostics = importDiagnosticsJson(assetService->lastImportDiagnostics());
                }
            }

            return toolJson({{"ok", true},
                             {"url", url},
                             {"uri", uri},
                             {"path", targetPath->generic_string()},
                             {"bytesDownloaded", bytesDownloaded},
                             {"reimported", imported},
                             {"diagnostics", std::move(diagnostics)}});
        }

        if (name == "vultra.assets.import_package")
        {
            const auto packagePath = std::filesystem::path(args.value("path", std::string {})).lexically_normal();
            if (packagePath.empty())
                return toolError("assets.import_package requires path");
            const auto assetRoot = projectAssetRoot(ctx);
            if (assetRoot.empty())
                return toolError("no project is loaded");

            const auto result = importVultraPackage(assetRoot, packagePath);
            nlohmann::json sourcePaths = nlohmann::json::array();
            for (const auto& path : result.sourcePaths)
            {
                sourcePaths.push_back(path.generic_string());
                ctx.state.pendingAssetImportPaths.push_back(path);
            }
            if (!result.sourcePaths.empty())
                ctx.state.pendingAssetImportRefresh = true;
            ++ctx.state.assetFileGeneration;
            return toolJson({{"ok", result.ok},
                             {"error", result.error},
                             {"package", packagePath.generic_string()},
                             {"filesWritten", result.filesWritten},
                             {"filesSkipped", result.filesSkipped},
                             {"queuedImport", !result.sourcePaths.empty()},
                             {"sourcePaths", std::move(sourcePaths)}});
        }

        return nullptr;
    }
} // namespace vultra_app
