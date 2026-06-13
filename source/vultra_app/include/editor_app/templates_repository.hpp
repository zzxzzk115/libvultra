#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Remote project-template catalog for the launcher's New Project page. Mirrors the examples
// repository pattern, but a template is scaffolding for a *new* project (no .vproject of its own --
// the launcher writes that) and its content is laid out per engine version: a template repository
// holds one folder per engine version (e.g. "0.1.0/") so different engine versions can ship
// different templates. The launcher copies the folder matching the running engine version (with a
// best-match / "default" fallback) into the new project directory.
//
// Launcher-global cache layout (rooted at cacheRoot):
//   .vultra/templates/
//     catalogs/<owner>-<repo>.json
//     thumbnails/<id>.png
//     .cache/<owner>-<repo>/        git clone cache
namespace vultra_app::templates
{
    [[nodiscard]] std::string defaultTemplatesCatalogUrl();

    struct TemplateEntry
    {
        std::string id;
        std::string name;
        std::string description;
        std::string thumbnailUrl;
        std::string gitUrl;
        // editingRenderGraph to write into the new project's .vproject (empty = none).
        std::string defaultRenderGraph;
    };

    bool fetchTemplatesCatalog(const std::filesystem::path& cacheRoot,
                               const std::string&           location,
                               std::vector<TemplateEntry>&  entries,
                               std::string&                 status);

    [[nodiscard]] std::filesystem::path cachedThumbnail(const std::filesystem::path& cacheRoot,
                                                        const TemplateEntry&         entry,
                                                        std::string&                 status);

    // Clone the template repo (cached) and copy the folder matching engineVersion (exact, else the
    // highest version folder <= engineVersion, else a "default" folder) into destDir. destDir is
    // created/replaced. Returns false with status on failure.
    [[nodiscard]] bool materializeTemplate(const std::filesystem::path& cacheRoot,
                                           const TemplateEntry&         entry,
                                           const std::string&           engineVersion,
                                           const std::filesystem::path& destDir,
                                           std::string&                 status);
} // namespace vultra_app::templates
