#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Remote catalog of prebuilt runtime ("export template") binaries for the editor's Export Settings.
// Mirrors the templates/examples repository pattern, but an export template is the editor-free
// vultra-runtime executable that the exporter copies next to the packed .vpk, identified uniquely by
// platform + arch + version. The binaries are NOT in the catalog repo -- the catalog only points at
// GitHub Release assets, which are downloaded on demand. See ../vultra-export-templates.
//
// Editor-global cache layout (rooted at cacheRoot):
//   .vultra/export-templates/
//     catalogs/<owner>-<repo>.json          downloaded catalog (offline fallback)
//     <platform>-<arch>/<version>/vultra-runtime[.exe]   materialized runtime binaries
namespace vultra_app::export_templates
{
    [[nodiscard]] std::string defaultExportTemplatesCatalogUrl();

    // Map the Export Settings platform label ("Windows"/"macOS"/"Linux"/"Android"/"WebGPU") to the
    // catalog's lowercase platform key ("windows"/"macos"/"linux"/"android"/"wasm"). Single mapping
    // point shared by the UI and the build-time resolver.
    [[nodiscard]] std::string catalogPlatform(const std::string& targetPlatform);

    // Runtime executable filename for a catalog platform ("vultra-runtime.exe" on windows, else
    // "vultra-runtime").
    [[nodiscard]] std::string runtimeFileName(const std::string& platform);

    struct ExportTemplateVersion
    {
        std::string   version;          // runtime/template version, e.g. "0.1.0"
        std::string   minEngineVersion; // lowest engine version this binary supports (empty = any)
        std::string   url;              // release-asset download URL
        std::string   sha256;           // optional integrity hash (hex); informational for now
        std::uint64_t size {0};         // optional expected byte size (0 = unknown)
    };

    struct ExportTemplateEntry
    {
        std::string                       id;       // e.g. "windows-x64"
        std::string                       platform; // "windows"/"macos"/"linux"/...
        std::string                       arch;     // "x64"/"arm64"
        std::string                       name;     // display label
        std::vector<ExportTemplateVersion> versions; // newest first
    };

    // Fetch + parse the catalog from an http(s) URL or a local path. Remote downloads are cached
    // under the export-templates store's catalogs/ folder; on download failure a previously cached
    // copy is used so the catalog still renders offline.
    bool fetchExportTemplatesCatalog(const std::filesystem::path&      cacheRoot,
                                     const std::string&                location,
                                     std::vector<ExportTemplateEntry>& entries,
                                     std::string&                      status);

    // Locate the entry for a platform+arch pair (nullptr if absent).
    [[nodiscard]] const ExportTemplateEntry* findEntry(const std::vector<ExportTemplateEntry>& entries,
                                                       const std::string&                      platform,
                                                       const std::string&                      arch);

    // Pick the best version for the running engine: an exact version match, else the highest version
    // whose minEngineVersion is satisfied by engineVersion. nullptr if none qualifies.
    [[nodiscard]] const ExportTemplateVersion* bestVersion(const ExportTemplateEntry& entry,
                                                           const std::string&         engineVersion);

    // Download (once; skipped if already present and the expected size matches) the runtime binary
    // into the cache and return its path. Empty path with status on failure.
    [[nodiscard]] std::filesystem::path materializeExportTemplate(const std::filesystem::path& cacheRoot,
                                                                  const ExportTemplateEntry&   entry,
                                                                  const ExportTemplateVersion& version,
                                                                  std::string&                 status);

    // Offline resolver for the build pipeline: return the cached runtime path for platform+arch best
    // matching engineVersion (no network). Empty path if nothing is cached.
    [[nodiscard]] std::filesystem::path cachedExportTemplate(const std::filesystem::path& cacheRoot,
                                                             const std::string&           platform,
                                                             const std::string&           arch,
                                                             const std::string&           engineVersion);
} // namespace vultra_app::export_templates
