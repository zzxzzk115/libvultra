#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace vultra_app
{
    enum class ProjectTemplateKind
    {
        Empty,
        Minimal,
    };

    ProjectTemplateKind projectTemplateKindFromString(std::string_view value);
    const char*         projectTemplateKindName(ProjectTemplateKind kind);

    bool writeProjectTemplateAssets(const std::filesystem::path& projectDir,
                                    ProjectTemplateKind          kind,
                                    std::string&                 errorMessage);
} // namespace vultra_app
