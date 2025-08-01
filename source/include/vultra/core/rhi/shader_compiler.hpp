#pragma once

#include "vultra/core/rhi/shader_type.hpp"

#include <filesystem>
#include <unordered_map>

namespace vultra
{
    namespace rhi
    {
        class ShaderCompiler final
        {
        public:
            ShaderCompiler();
            ShaderCompiler(const ShaderCompiler&)     = delete;
            ShaderCompiler(ShaderCompiler&&) noexcept = delete;
            ~ShaderCompiler();

            ShaderCompiler& operator=(const ShaderCompiler&)     = delete;
            ShaderCompiler& operator=(ShaderCompiler&&) noexcept = delete;

            using ErrorMessage = std::string;
            using Result       = std::expected<SPIRV, ErrorMessage>;

            [[nodiscard]] static Result
            compile(const ShaderType,
                    const std::string_view                                             code,
                    const std::string_view                                             entryPointName,
                    const std::unordered_map<std::string, std::optional<std::string>>& defines);

            static void setShaderRootPath(const std::filesystem::path& path) { s_ShaderRootPath = path; }
            [[nodiscard]] static const std::filesystem::path& getShaderRootPath() { return s_ShaderRootPath; }

        private:
            static std::filesystem::path s_ShaderRootPath;
        };
    } // namespace rhi
} // namespace vultra