#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>

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
        explicit ShaderCompiler(std::filesystem::path cacheDirectory = ".vultra/shaders");
        ~ShaderCompiler();
        ShaderCompiler(const ShaderCompiler&)            = delete;
        ShaderCompiler& operator=(const ShaderCompiler&) = delete;

        ShaderProgram compile(const std::filesystem::path& file, const ShaderCompileOptions& options = {});
        ShaderProgram compileSource(const std::filesystem::path& file,
                                    std::string_view             text,
                                    const ShaderCompileOptions&  options = {});
        const ShaderCompileStatistics& lastCompileStatistics() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
