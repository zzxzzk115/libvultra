#pragma once

#include "vultra/core/rhi/structs/shader_type.hpp"

#include <expected>
#include <filesystem>
#include <unordered_map>

// Shader compilation is provided by vshadersystem.
// libvultra intentionally does not depend on glslang/spirv-cross directly.
#include <vshadersystem/compiler.hpp>

namespace vultra
{
    namespace rhi
    {
        class ShaderCompiler final
        {
        public:
            ShaderCompiler()                          = default;
            ShaderCompiler(const ShaderCompiler&)     = delete;
            ShaderCompiler(ShaderCompiler&&) noexcept = delete;
            ~ShaderCompiler()                         = default;

            ShaderCompiler& operator=(const ShaderCompiler&)     = delete;
            ShaderCompiler& operator=(ShaderCompiler&&) noexcept = delete;

            using ErrorMessage = std::string;
            using Result       = std::expected<SPIRV, ErrorMessage>;

            [[nodiscard]] Result
            compile(const ShaderType,
                    const std::string_view                                             code,
                    const std::string_view                                             entryPointName,
                    const std::unordered_map<std::string, std::optional<std::string>>& defines) const;

            static void setShaderRootPath(const std::filesystem::path& path) { s_ShaderRootPath = path; }
            [[nodiscard]] static const std::filesystem::path& getShaderRootPath() { return s_ShaderRootPath; }

        private:
            static std::filesystem::path s_ShaderRootPath;
        };
    } // namespace rhi
} // namespace vultra
