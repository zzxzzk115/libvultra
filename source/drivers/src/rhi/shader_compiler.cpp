#include "shader_archive.hpp"
#include "shader_cache.hpp"
#include "shader_file_system.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/shader_compiler.hpp>

#include <slang-com-ptr.h>
#include <slang.h>
#include <xxhash.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double milliseconds(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        uint32_t colorOutputs(slang::VariableLayoutReflection* variable)
        {
            if (!variable)
            {
                return 0;
            }
            if (const auto* semantic = variable->getSemanticName())
            {
                std::string name = semantic;
                std::ranges::transform(name,
                                       name.begin(),
                                       [](unsigned char value)
                                       {
                                           return char(std::tolower(value));
                                       });
                if (name == "sv_target")
                {
                    return uint32_t(variable->getSemanticIndex()) + 1;
                }
            }
            uint32_t count = 0;
            auto*    type  = variable->getTypeLayout();
            for (uint32_t i = 0; i < type->getFieldCount(); ++i)
            {
                count = std::max(count, colorOutputs(type->getFieldByIndex(i)));
            }
            return count;
        }

        VriShaderStageBits shaderStage(SlangStage stage)
        {
            switch (stage)
            {
                case SLANG_STAGE_VERTEX:
                    return VriShaderStage_Vertex;
                case SLANG_STAGE_FRAGMENT:
                    return VriShaderStage_Fragment;
                case SLANG_STAGE_COMPUTE:
                    return VriShaderStage_Compute;
                case SLANG_STAGE_GEOMETRY:
                    return VriShaderStage_Geometry;
                case SLANG_STAGE_HULL:
                    return VriShaderStage_TessControl;
                case SLANG_STAGE_DOMAIN:
                    return VriShaderStage_TessEval;
                case SLANG_STAGE_AMPLIFICATION:
                    return VriShaderStage_Task;
                case SLANG_STAGE_MESH:
                    return VriShaderStage_Mesh;
                case SLANG_STAGE_RAY_GENERATION:
                    return VriShaderStage_RayGen;
                case SLANG_STAGE_INTERSECTION:
                    return VriShaderStage_Intersection;
                case SLANG_STAGE_ANY_HIT:
                    return VriShaderStage_AnyHit;
                case SLANG_STAGE_CLOSEST_HIT:
                    return VriShaderStage_ClosestHit;
                case SLANG_STAGE_MISS:
                    return VriShaderStage_Miss;
                case SLANG_STAGE_CALLABLE:
                    return VriShaderStage_Callable;
                default:
                    throw std::invalid_argument("Unsupported cooked shader stage");
            }
        }

    } // namespace

    struct ShaderCompiler::Impl
    {
        struct Module
        {
            std::string                   signature;
            Slang::ComPtr<ISlangBlob>     code;
            std::vector<ShaderDependency> dependencies;
            std::string                   diagnostics;
        };

        std::filesystem::path                cacheDirectory;
        Slang::ComPtr<slang::IGlobalSession> global;
        std::vector<Module>                  modules;
        ShaderCompileStatistics              statistics;
    };

    ShaderCompiler::ShaderCompiler(std::filesystem::path cacheDirectory) :
        m_Impl(std::make_unique<Impl>())
    {
        if (!cacheDirectory.empty())
        {
            m_Impl->cacheDirectory = std::filesystem::absolute(cacheDirectory).lexically_normal();
        }
    }

    ShaderCompiler::~ShaderCompiler() = default;

    const ShaderCompileStatistics& ShaderCompiler::lastCompileStatistics() const
    {
        return m_Impl->statistics;
    }

    ShaderProgram ShaderCompiler::compile(const std::filesystem::path& file, const ShaderCompileOptions& options)
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Open Slang source: " + file.string());
        }
        const std::string text {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        return compileSource(file, text, options);
    }

    ShaderProgram ShaderCompiler::compileSource(const std::filesystem::path& sourcePath,
                                                std::string_view             sourceText,
                                                const ShaderCompileOptions&  compileOptions)
    {
        using Slang::ComPtr;
        const auto started               = Clock::now();
        auto&      statistics            = m_Impl->statistics;
        statistics                       = {};
        const auto    requested          = std::span(compileOptions.entries);
        const auto&   includeDirectories = compileOptions.includeDirectories;
        const bool    rayQuery           = compileOptions.rayQuery;
        ShaderProgram program;
        program.profile      = compileOptions.profile;
        program.rayQuery     = rayQuery;
        program.capabilities = compileOptions.capabilities;
        if (sourceText.find(char(0)) != std::string_view::npos)
        {
            throw std::invalid_argument("Shader source contains a NUL character: " + sourcePath.string());
        }
        const auto signature = detail::shaderCompileSignature(sourcePath, sourceText, compileOptions);
        program.compileKey   = std::to_string(XXH3_64bits(signature.data(), signature.size()));
        const auto cacheFile = m_Impl->cacheDirectory.empty() ?
                                   std::filesystem::path {} :
                                   m_Impl->cacheDirectory / (program.compileKey + ".vshadercache");
        if (!cacheFile.empty() && std::filesystem::is_regular_file(cacheFile))
        {
            try
            {
                const auto document = detail::readShaderArchive(cacheFile);
                if (document.at("version") != 1 || document.at("kind") != "development_shader")
                {
                    throw std::runtime_error("Unsupported development shader cache");
                }
                // A hash selects a file; the complete request establishes equality.
                if (document.at("request") == signature)
                {
                    auto cached = detail::decodeShaderProgram(document.at("program"));
                    detail::validateShaderProgram(cached);
                    if (cached.compileKey == program.compileKey &&
                        detail::shaderDependenciesCurrent(cached.dependencies, sourcePath, compileOptions))
                    {
                        cached.diagnostics           = document.at("diagnostics").get<std::string>();
                        statistics.programCacheHit   = true;
                        statistics.totalMilliseconds = milliseconds(started);
                        Logger::core().info("Shader program cache hit: {} ({:.1f} ms)",
                                            sourcePath.string(),
                                            statistics.totalMilliseconds);
                        return cached;
                    }
                }
            }
            catch (const std::exception& error)
            {
                Logger::core().warn("Rebuilding unreadable development shader cache {}: {}",
                                    cacheFile.string(),
                                    error.what());
            }
        }
        const auto frontendStarted = Clock::now();
        const auto source          = std::filesystem::absolute(sourcePath).lexically_normal();
        auto       diagnose        = [&](SlangResult result, const ComPtr<slang::IBlob>& blob, const char* operation)
        {
            if (blob)
            {
                program.diagnostics += static_cast<const char*>(blob->getBufferPointer());
            }
            if (SLANG_FAILED(result))
            {
                throw std::runtime_error(source.string() + ": " + operation + "\n" + program.diagnostics);
            }
        };
        auto& global = m_Impl->global;
        if (!global)
        {
            diagnose(slang::createGlobalSession(global.writeRef()), {}, "Create Slang global session");
        }
        slang::TargetDesc target {};
        target.format  = SLANG_SPIRV;
        target.profile = global->findProfile(program.profile.c_str());
        if (!program.profile.starts_with("spirv_") || target.profile == SLANG_PROFILE_UNKNOWN)
        {
            throw std::invalid_argument("Unsupported SPIR-V profile: " + program.profile);
        }
        std::vector<slang::CompilerOptionEntry> options(2);
        options[0].name = slang::CompilerOptionName::VulkanUseEntryPointName;
        options[1].name = slang::CompilerOptionName::EmitSpirvDirectly;
        for (auto& option : options)
        {
            option.value.kind      = slang::CompilerOptionValueKind::Int;
            option.value.intValue0 = 1;
        }
#ifndef NDEBUG
        slang::CompilerOptionEntry debugInfo {};
        debugInfo.name            = slang::CompilerOptionName::DebugInformation;
        debugInfo.value.kind      = slang::CompilerOptionValueKind::Int;
        debugInfo.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_STANDARD;
        options.push_back(debugInfo);
#endif
        auto capabilities = compileOptions.capabilities;
        if (rayQuery)
        {
            capabilities.push_back("spvRayQueryKHR");
        }
        for (const auto& name : capabilities)
        {
            slang::CompilerOptionEntry capability {};
            capability.name            = slang::CompilerOptionName::Capability;
            capability.value.kind      = slang::CompilerOptionValueKind::Int;
            capability.value.intValue0 = global->findCapability(name.c_str());
            if (capability.value.intValue0 == SLANG_CAPABILITY_UNKNOWN)
            {
                throw std::invalid_argument("Unknown Slang capability: " + name);
            }
            options.push_back(capability);
        }
        std::vector<std::string> paths {source.parent_path().string()};
        for (const auto& directory : includeDirectories)
        {
            paths.push_back(std::filesystem::absolute(directory).lexically_normal().string());
        }
        for (const auto& path : compileOptions.linkModules)
        {
            const auto resolved = path.is_absolute() ? path : source.parent_path() / path;
            paths.push_back(std::filesystem::absolute(resolved).parent_path().string());
        }
        std::vector<const char*> searchPaths;
        for (const auto& directory : paths)
        {
            searchPaths.push_back(directory.c_str());
        }
        std::vector<slang::PreprocessorMacroDesc> macros;
        for (const auto& define : compileOptions.defines)
        {
            macros.push_back({define.name.c_str(), define.value.c_str()});
        }
        ComPtr<detail::ShaderFileSystem> fileSystem;
        fileSystem.attach(new detail::ShaderFileSystem);
        // Track the authored primary file separately from generated in-memory adapter text.
        if (std::filesystem::is_regular_file(source))
        {
            ComPtr<ISlangBlob> primary;
            diagnose(fileSystem->loadFile(source.string().c_str(), primary.writeRef()),
                     {},
                     "Snapshot primary shader source");
        }
        slang::SessionDesc desc {};
        desc.fileSystem               = fileSystem;
        desc.targets                  = &target;
        desc.targetCount              = 1;
        desc.searchPaths              = searchPaths.data();
        desc.searchPathCount          = SlangInt(searchPaths.size());
        desc.preprocessorMacros       = macros.data();
        desc.preprocessorMacroCount   = SlangInt(macros.size());
        desc.defaultMatrixLayoutMode  = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
        desc.compilerOptionEntries    = options.data();
        desc.compilerOptionEntryCount = uint32_t(options.size());
        // Sessions remain isolated: link constants cannot leak between variants.
        ComPtr<slang::ISession> session;
        diagnose(global->createSession(desc, session.writeRef()), {}, "Create Slang session");
        ComPtr<slang::IBlob> diagnostics;
        const std::string    sourceBuffer(sourceText);
        auto                 frontendOptions = compileOptions;
        frontendOptions.entries.clear();
        frontendOptions.linkSources.clear();
        for (const auto& path : compileOptions.linkModules)
        {
            const auto resolved = path.is_absolute() ? path : source.parent_path() / path;
            frontendOptions.includeDirectories.push_back(std::filesystem::absolute(resolved).parent_path());
        }
        frontendOptions.linkModules.clear();
        const auto frontendSignature = detail::shaderCompileSignature(source, sourceText, frontendOptions);
        const auto previous          = std::ranges::find_if(m_Impl->modules,
                                                   [&](const Impl::Module& item)
                                                   {
                                                       return item.signature == frontendSignature;
                                                   });
        std::vector<ShaderDependency> moduleDependencies;
        slang::IModule*               module = nullptr;
        ComPtr<ISlangBlob>            moduleCode;
        std::string                   moduleDiagnostics;
        if (previous != m_Impl->modules.end() &&
            detail::shaderDependenciesCurrent(previous->dependencies, source, frontendOptions))
        {
            module = session->loadModuleFromIRBlob(source.stem().string().c_str(),
                                                   source.string().c_str(),
                                                   previous->code,
                                                   diagnostics.writeRef());
            diagnose(module ? SLANG_OK : SLANG_FAIL, diagnostics, "Load checked Slang module");
            moduleDependencies = previous->dependencies;
            program.diagnostics += previous->diagnostics;
            statistics.moduleCacheHit = true;
        }
        else
        {
            module = session->loadModuleFromSourceString(source.stem().string().c_str(),
                                                         source.string().c_str(),
                                                         sourceBuffer.c_str(),
                                                         diagnostics.writeRef());
            diagnose(module ? SLANG_OK : SLANG_FAIL, diagnostics, "Load shader module");
            moduleDependencies = fileSystem->dependencies();
            moduleDiagnostics  = program.diagnostics;
            diagnose(module->serialize(moduleCode.writeRef()), {}, "Serialize checked Slang module");
        }
        std::vector<ComPtr<slang::IEntryPoint>> entries;
        if (requested.empty())
        {
            entries.resize(size_t(module->getDefinedEntryPointCount()));
            for (size_t i = 0; i < entries.size(); ++i)
            {
                diagnose(module->getDefinedEntryPoint(SlangInt32(i), entries[i].writeRef()),
                         {},
                         "Discover shader entry point");
            }
        }
        else
        {
            entries.resize(requested.size());
            for (size_t i = 0; i < entries.size(); ++i)
            {
                SlangStage stage = SLANG_STAGE_NONE;
                for (const auto candidate : {SLANG_STAGE_VERTEX,
                                             SLANG_STAGE_FRAGMENT,
                                             SLANG_STAGE_COMPUTE,
                                             SLANG_STAGE_GEOMETRY,
                                             SLANG_STAGE_HULL,
                                             SLANG_STAGE_DOMAIN,
                                             SLANG_STAGE_AMPLIFICATION,
                                             SLANG_STAGE_MESH,
                                             SLANG_STAGE_RAY_GENERATION,
                                             SLANG_STAGE_INTERSECTION,
                                             SLANG_STAGE_ANY_HIT,
                                             SLANG_STAGE_CLOSEST_HIT,
                                             SLANG_STAGE_MISS,
                                             SLANG_STAGE_CALLABLE})
                {
                    if (shaderStage(candidate) == requested[i].stage)
                    {
                        stage = candidate;
                        break;
                    }
                }
                diagnostics.setNull();
                diagnose(module->findAndCheckEntryPoint(requested[i].name.c_str(),
                                                        stage,
                                                        entries[i].writeRef(),
                                                        diagnostics.writeRef()),
                         diagnostics,
                         "Find shader entry point");
            }
        }
        if (entries.empty())
        {
            throw std::invalid_argument("No annotated shader entries: " + source.string());
        }
        std::vector<slang::IComponentType*> components {module};
        for (const auto& entry : entries)
        {
            components.push_back(entry);
        }
        for (const auto& path : compileOptions.linkModules)
        {
            const auto         resolved = path.is_absolute() ? path : source.parent_path() / path;
            ComPtr<ISlangBlob> linkedBytes;
            diagnose(fileSystem->loadFile(resolved.string().c_str(), linkedBytes.writeRef()),
                     {},
                     "Snapshot linked Slang module");
            const std::string linkedSource(static_cast<const char*>(linkedBytes->getBufferPointer()),
                                           linkedBytes->getBufferSize());
            diagnostics.setNull();
            auto* linkedModule = session->loadModuleFromSourceString(resolved.stem().string().c_str(),
                                                                     resolved.string().c_str(),
                                                                     linkedSource.c_str(),
                                                                     diagnostics.writeRef());
            diagnose(linkedModule ? SLANG_OK : SLANG_FAIL, diagnostics, "Load linked Slang module");
            components.push_back(linkedModule);
        }
        for (const auto& linkSource : compileOptions.linkSources)
        {
            diagnostics.setNull();
            const auto syntheticPath = source.parent_path() / (linkSource.name + ".slang");
            auto*      linkedModule  = session->loadModuleFromSourceString(linkSource.name.c_str(),
                                                                     syntheticPath.string().c_str(),
                                                                     linkSource.value.c_str(),
                                                                     diagnostics.writeRef());
            diagnose(linkedModule ? SLANG_OK : SLANG_FAIL, diagnostics, "Load link-time Slang source");
            components.push_back(linkedModule);
        }
        statistics.frontendMilliseconds           = milliseconds(frontendStarted);
        const auto                    linkStarted = Clock::now();
        ComPtr<slang::IComponentType> composed;
        ComPtr<slang::IComponentType> linked;
        diagnostics.setNull();
        auto result = session->createCompositeComponentType(components.data(),
                                                            SlangInt(components.size()),
                                                            composed.writeRef(),
                                                            diagnostics.writeRef());
        diagnose(result, diagnostics, "Compose shader");
        diagnostics.setNull();
        result = composed->link(linked.writeRef(), diagnostics.writeRef());
        diagnose(result, diagnostics, "Link shader");
        diagnostics.setNull();
        auto* layout = linked->getLayout(0, diagnostics.writeRef());
        diagnose(layout ? SLANG_OK : SLANG_FAIL, diagnostics, "Reflect shader entries");
        statistics.linkMilliseconds = milliseconds(linkStarted);
        const auto codegenStarted   = Clock::now();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto*       reflection = layout->getEntryPointByIndex(SlangUInt(i));
            ShaderEntry entry {reflection->getName(), shaderStage(reflection->getStage())};
            if (!requested.empty() && entry.stage != requested[i].stage)
            {
                throw std::invalid_argument("Shader stage does not match declaration: " + entry.name);
            }
            ComPtr<slang::IBlob> code;
            diagnostics.setNull();
            result = linked->getEntryPointCode(SlangInt(i), 0, code.writeRef(), diagnostics.writeRef());
            diagnose(result, diagnostics, "Emit SPIR-V");
            if (code->getBufferSize() % sizeof(uint32_t) != 0)
            {
                throw std::runtime_error("Slang emitted misaligned SPIR-V: " + entry.name);
            }
            CompiledShader shader {std::move(entry), std::vector<uint32_t>(code->getBufferSize() / sizeof(uint32_t))};
            if (shader.entry.stage == VriShaderStage_Fragment)
            {
                shader.colorOutputs = colorOutputs(reflection->getResultVarLayout());
                for (uint32_t parameter = 0; parameter < reflection->getParameterCount(); ++parameter)
                {
                    shader.colorOutputs =
                        std::max(shader.colorOutputs, colorOutputs(reflection->getParameterByIndex(parameter)));
                }
            }
            if (shader.entry.stage == VriShaderStage_Compute || shader.entry.stage == VriShaderStage_Task ||
                shader.entry.stage == VriShaderStage_Mesh)
            {
                SlangUInt sizes[3] {};
                reflection->getComputeThreadGroupSize(3, sizes);
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    shader.threadGroup[axis] = uint32_t(sizes[axis]);
                }
            }
            std::memcpy(shader.words.data(), code->getBufferPointer(), code->getBufferSize());
            program.shaders.push_back(std::move(shader));
        }
        program.parameters             = detail::reflectShaderParameters(layout->getGlobalParamsVarLayout());
        statistics.codegenMilliseconds = milliseconds(codegenStarted);
        program.dependencies           = fileSystem->dependencies();
        for (const auto& dependency : moduleDependencies)
        {
            const auto found = std::ranges::find(program.dependencies, dependency.path, &ShaderDependency::path);
            if (found == program.dependencies.end())
            {
                program.dependencies.push_back(dependency);
            }
            else if (found->hash != dependency.hash)
            {
                throw std::runtime_error("Shader input changed between frontend and link: " + dependency.path.string());
            }
        }
        if (!detail::shaderDependenciesCurrent(program.dependencies, source, compileOptions))
        {
            throw std::runtime_error("Shader dependency changed while compiling: " + source.string());
        }
        if (detail::shaderCompileSignature(sourcePath, sourceText, compileOptions) != signature)
        {
            throw std::runtime_error("Linked shader input changed while compiling: " + source.string());
        }
        detail::validateShaderProgram(program);
        if (moduleCode)
        {
            if (previous != m_Impl->modules.end())
            {
                m_Impl->modules.erase(previous);
            }
            if (m_Impl->modules.size() == 16)
            {
                m_Impl->modules.erase(m_Impl->modules.begin());
            }
            m_Impl->modules.push_back({frontendSignature,
                                       std::move(moduleCode),
                                       std::move(moduleDependencies),
                                       std::move(moduleDiagnostics)});
        }
        if (!cacheFile.empty())
        {
            try
            {
                detail::writeShaderArchive(cacheFile,
                                           {{"version", 1},
                                            {"kind", "development_shader"},
                                            {"request", signature},
                                            {"diagnostics", program.diagnostics},
                                            {"program", detail::encodeShaderProgram(program)}});
            }
            catch (const std::exception& error)
            {
                Logger::core().warn("Could not write development shader cache {}: {}",
                                    cacheFile.string(),
                                    error.what());
            }
        }
        statistics.totalMilliseconds = milliseconds(started);
        Logger::core().info(
            "Shader compiled: {} | frontend {:.1f} ms{}, link {:.1f} ms, SPIR-V {:.1f} ms, total {:.1f} ms",
            source.string(),
            statistics.frontendMilliseconds,
            statistics.moduleCacheHit ? " (IR reused)" : "",
            statistics.linkMilliseconds,
            statistics.codegenMilliseconds,
            statistics.totalMilliseconds);
        return program;
    }

} // namespace vultra
