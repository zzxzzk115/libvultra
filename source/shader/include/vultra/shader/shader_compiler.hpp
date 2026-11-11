#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/shader/shader_program.hpp>

#include <expected>
#include <memory>

namespace vultra
{
    struct ShaderCompileStatistics
    {
        bool   programCacheHit      = false;
        bool   moduleCacheHit       = false;
        double frontendMilliseconds = 0;
        double linkMilliseconds     = 0;
        double codegenMilliseconds  = 0;
        double totalMilliseconds    = 0;
    };

    // One caller owns this context; concurrent workers must use separate contexts.
    // An empty cache directory disables disk caching. Slang is initialized only on a cache miss.
    class ShaderCompiler
    {
    public:
        // The cache directory resolves against the current directory here.
        [[nodiscard]] static std::expected<ShaderCompiler, Error>
        create(const std::filesystem::path& cacheDirectory = ".vultra/shaders");
        ~ShaderCompiler();
        ShaderCompiler(ShaderCompiler&&) noexcept;
        ShaderCompiler& operator=(ShaderCompiler&&) noexcept;
        ShaderCompiler(const ShaderCompiler&)            = delete;
        ShaderCompiler& operator=(const ShaderCompiler&) = delete;

        [[nodiscard]] std::expected<ShaderProgram, Error> compile(const std::filesystem::path& file,
                                                                  const ShaderCompileOptions&  options = {});
        [[nodiscard]] std::expected<ShaderProgram, Error> compileSource(const std::filesystem::path& file,
                                                                        std::string_view             text,
                                                                        const ShaderCompileOptions&  options = {});
        const ShaderCompileStatistics&                    lastCompileStatistics() const;

    private:
        explicit ShaderCompiler(std::filesystem::path cacheDirectory);

        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
