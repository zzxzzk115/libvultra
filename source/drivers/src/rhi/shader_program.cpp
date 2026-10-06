#include "shader_archive.hpp"
#include "shader_cache.hpp"
#include "shader_toolchain_key.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/shader_compiler.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>
#include <slang.h>
#include <xxhash.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        constexpr size_t kMaxFileSize = 64 * 1024 * 1024;

        bool validStage(VriShaderStageBits stage)
        {
            return stage == VriShaderStage_Vertex || stage == VriShaderStage_Fragment ||
                   stage == VriShaderStage_Compute || stage == VriShaderStage_Geometry ||
                   stage == VriShaderStage_TessControl || stage == VriShaderStage_TessEval ||
                   stage == VriShaderStage_Task || stage == VriShaderStage_Mesh || stage == VriShaderStage_RayGen ||
                   stage == VriShaderStage_Intersection || stage == VriShaderStage_AnyHit ||
                   stage == VriShaderStage_ClosestHit || stage == VriShaderStage_Miss ||
                   stage == VriShaderStage_Callable;
        }

    } // namespace

    void detail::validateShaderProgram(const ShaderProgram& program)
    {
        if (program.shaders.empty() || program.shaders.size() > 256)
        {
            throw std::invalid_argument("Shader program needs between 1 and 256 entries");
        }
        std::set<std::string> names;
        for (const auto& shader : program.shaders)
        {
            const auto& name = shader.entry.name;
            if (name.empty() || name.size() > 4096 || name.find(char(0)) != std::string::npos ||
                !names.insert(name).second || !validStage(shader.entry.stage))
            {
                throw std::invalid_argument("Invalid or duplicate shader entry: " + name);
            }
            if (shader.words.size() < 5 || shader.words.front() != 0x07230203 ||
                shader.words.size() > kMaxFileSize / sizeof(uint32_t))
            {
                throw std::invalid_argument("Invalid SPIR-V bytecode: " + name);
            }
        }
    }

    ShaderProgram ShaderProgram::compile(const std::filesystem::path&           file,
                                         std::span<const ShaderEntry>           requested,
                                         std::span<const std::filesystem::path> includeDirectories,
                                         bool                                   rayQuery)
    {
        ShaderCompileOptions options;
        options.entries.assign(requested.begin(), requested.end());
        options.includeDirectories.assign(includeDirectories.begin(), includeDirectories.end());
        options.rayQuery = rayQuery;
        return compile(file, options);
    }

    ShaderProgram ShaderProgram::compile(const std::filesystem::path& file, const ShaderCompileOptions& options)
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Open Slang source: " + file.string());
        }
        const std::string source {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        return compileSource(file, source, options);
    }

    ShaderProgram ShaderProgram::compileSource(const std::filesystem::path& file,
                                               std::string_view             text,
                                               const ShaderCompileOptions&  options)
    {
        ShaderCompiler compiler;
        return compiler.compileSource(file, text, options);
    }

    std::vector<VriShaderDesc> ShaderProgram::descriptors(std::span<const ShaderEntry> entries) const
    {
        if (entries.empty())
        {
            throw std::invalid_argument("Select at least one shader entry");
        }
        std::vector<VriShaderDesc> result;
        for (const auto& entry : entries)
        {
            const auto found =
                std::find_if(shaders.begin(),
                             shaders.end(),
                             [&](const CompiledShader& shader)
                             {
                                 return shader.entry.name == entry.name && shader.entry.stage == entry.stage;
                             });
            if (found == shaders.end())
            {
                throw std::invalid_argument("Shader program has no matching entry/stage: " + entry.name);
            }
            VriShaderDesc desc {};
            desc.stage          = found->entry.stage;
            desc.bytecode       = found->words.data();
            desc.bytecodeSize   = found->words.size() * sizeof(uint32_t);
            desc.entryPointName = found->entry.name.c_str();
            result.push_back(desc);
        }
        return result;
    }

    std::string ShaderProgram::compilerVersion()
    {
        std::string identity = std::string(spGetBuildTagString()) + ";" + kShaderToolchainKey;
#ifdef NDEBUG
        identity += ";debug=none";
#else
        identity += ";debug=standard";
#endif
        return identity;
    }

    bool ShaderProgram::cook(const std::filesystem::path& source,
                             const std::filesystem::path& output,
                             const ShaderCompileOptions&  options)
    {
        std::ifstream input(source, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Open Slang source: " + source.string());
        }
        const std::string text {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto        key = detail::shaderCompileKey(source, text, options);
        if (std::filesystem::is_regular_file(output))
        {
            try
            {
                const auto previous = load(output);
                if (previous.compileKey == key &&
                    detail::shaderDependenciesCurrent(previous.dependencies, source, options))
                {
                    Logger::core().info("Shader cache hit: {}", source.string());
                    return false;
                }
            }
            catch (const std::exception& error)
            {
                Logger::core().warn("Rebuilding unreadable shader cache {}: {}", output.string(), error.what());
            }
        }
        Logger::core().info("Cooking raw Slang shader: {}", source.string());
        auto candidate = compileSource(source, text, options);
        candidate.save(output);
        if (!candidate.diagnostics.empty())
        {
            Logger::core().warn("{}", candidate.diagnostics);
        }
        return true;
    }

    void ShaderProgram::save(const std::filesystem::path& file) const
    {
        detail::validateShaderProgram(*this);
        auto document       = detail::encodeShaderProgram(*this);
        document["version"] = 1;
        document["kind"]    = "raw_slang";
        detail::writeShaderArchive(file, document);
    }

    ShaderProgram ShaderProgram::load(const std::filesystem::path& file)
    {
        const auto document = detail::readShaderArchive(file);
        if (document.at("kind") != "raw_slang")
        {
            throw std::invalid_argument("ShaderProgram requires a raw Slang artifact: " + file.string());
        }
        auto program = detail::decodeShaderProgram(document);
        detail::validateShaderProgram(program);
        return program;
    }
} // namespace vultra
