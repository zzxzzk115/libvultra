#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>

#include <filesystem>

namespace vultra
{
    // Project export validates engine assets and cooks shaders; plain VPK access does neither.
    void packProject(const std::filesystem::path& projectFile,
                     const std::filesystem::path& output,
                     const ShaderCompileOptions&  shaders = {});
    void packBuiltins(const std::filesystem::path& engineRoot, const std::filesystem::path& output);
} // namespace vultra
