#include "shader_archive.hpp"
#include "shader_cache.hpp"
#include "shader_file_system.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/shader_compiler.hpp>

#include <slang-com-ptr.h>
#include <slang.h>
#include <spirv-tools/optimizer.hpp>
#include <xxhash.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
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

        std::expected<std::vector<uint32_t>, std::string>
        optimizeSpirvExpected(std::span<const uint32_t> words, std::string_view entry, std::string& diagnostics)
        {
            // Universal validation follows the module's actual SPIR-V version. VRI validates native API features.
            spvtools::Optimizer optimizer(SPV_ENV_UNIVERSAL_1_6);
            std::string         messages;
            optimizer.SetMessageConsumer(
                [&](spv_message_level_t level, const char*, const spv_position_t& position, const char* message)
                {
                    messages += "SPIRV-Tools entry " + std::string(entry) + " at word " +
                                std::to_string(position.index) + " (level " + std::to_string(int(level)) +
                                "): " + message + "\n";
                });
            // Keep the pinned performance sequence explicit. Its second redundancy elimination stalls textured
            // visibility shading on Adreno 740; retain the remaining passes and Slang's reflected resource ABI.
            optimizer.RegisterPass(spvtools::CreateWrapOpKillPass())
                .RegisterPass(spvtools::CreateDeadBranchElimPass())
                .RegisterPass(spvtools::CreateMergeReturnPass())
                .RegisterPass(spvtools::CreateInlineExhaustivePass())
                .RegisterPass(spvtools::CreateEliminateDeadFunctionsPass())
                .RegisterPass(spvtools::CreateAggressiveDCEPass(true))
                .RegisterPass(spvtools::CreatePrivateToLocalPass())
                .RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass())
                .RegisterPass(spvtools::CreateLocalSingleStoreElimPass())
                .RegisterPass(spvtools::CreateAggressiveDCEPass(true))
                .RegisterPass(spvtools::CreateScalarReplacementPass(0))
                .RegisterPass(spvtools::CreateLocalAccessChainConvertPass())
                .RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass())
                .RegisterPass(spvtools::CreateLocalSingleStoreElimPass())
                .RegisterPass(spvtools::CreateAggressiveDCEPass(true))
                .RegisterPass(spvtools::CreateLocalMultiStoreElimPass())
                .RegisterPass(spvtools::CreateAggressiveDCEPass(true))
                .RegisterPass(spvtools::CreateCCPPass())
                .RegisterPass(spvtools::CreateAggressiveDCEPass(true))
                .RegisterPass(spvtools::CreateLoopUnrollPass(true))
                .RegisterPass(spvtools::CreateDeadBranchElimPass())
                .RegisterPass(spvtools::CreateRedundancyEliminationPass())
                .RegisterPass(spvtools::CreateCombineAccessChainsPass())
                .RegisterPass(spvtools::CreateSimplificationPass())
                .RegisterPass(spvtools::CreateScalarReplacementPass(0))
                .RegisterPass(spvtools::CreateLocalAccessChainConvertPass())
                .RegisterPass(spvtools::CreateLocalSingleBlockLoadStoreElimPass())
                .RegisterPass(spvtools::CreateLocalSingleStoreElimPass())
                .RegisterPass(spvtools::CreateAggressiveDCEPass(true))
                .RegisterPass(spvtools::CreateSSARewritePass())
                .RegisterPass(spvtools::CreateAggressiveDCEPass(true))
                .RegisterPass(spvtools::CreateVectorDCEPass())
                .RegisterPass(spvtools::CreateDeadInsertElimPass())
                .RegisterPass(spvtools::CreateDeadBranchElimPass())
                .RegisterPass(spvtools::CreateSimplificationPass())
                .RegisterPass(spvtools::CreateIfConversionPass())
                .RegisterPass(spvtools::CreateCopyPropagateArraysPass())
                .RegisterPass(spvtools::CreateReduceLoadSizePass())
                .RegisterPass(spvtools::CreateAggressiveDCEPass(true))
                .RegisterPass(spvtools::CreateBlockMergePass())
                .RegisterPass(spvtools::CreateDeadBranchElimPass())
                .RegisterPass(spvtools::CreateBlockMergePass())
                .RegisterPass(spvtools::CreateSimplificationPass());
            spvtools::OptimizerOptions options;
            options.set_preserve_bindings(true);
            std::vector<uint32_t> optimized;
            if (!optimizer.Run(words.data(), words.size(), &optimized, options))
            {
                return std::unexpected("Optimize SPIR-V entry " + std::string(entry) + "\n" + messages);
            }
            diagnostics += messages;
            return optimized;
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

        std::expected<VriShaderStageBits, std::string> shaderStage(SlangStage stage)
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
                    return std::unexpected("Unsupported cooked shader stage");
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

    ShaderCompiler::ShaderCompiler(Uninitialized) :
        m_Impl(std::make_unique<Impl>())
    {
    }

    std::expected<void, std::string> ShaderCompiler::initialize(std::filesystem::path cacheDirectory)
    {
        if (!cacheDirectory.empty())
        {
            auto absolute = detail::shaderAbsolutePathExpected(cacheDirectory);
            if (!absolute)
            {
                return std::unexpected(absolute.error());
            }
            m_Impl->cacheDirectory = std::move(*absolute);
        }
        return {};
    }

    ShaderCompiler::ShaderCompiler(std::filesystem::path cacheDirectory) :
        ShaderCompiler(Uninitialized {})
    {
        if (auto ready = initialize(std::move(cacheDirectory)); !ready)
        {
            throw std::runtime_error(ready.error()); // Legacy construction boundary.
        }
    }

    std::expected<std::unique_ptr<ShaderCompiler>, std::string>
    ShaderCompiler::create(std::filesystem::path cacheDirectory)
    {
        auto compiler = std::unique_ptr<ShaderCompiler>(new ShaderCompiler(Uninitialized {}));
        if (auto ready = compiler->initialize(std::move(cacheDirectory)); !ready)
        {
            return std::unexpected(ready.error());
        }
        return compiler;
    }

    ShaderCompiler::~ShaderCompiler() = default;

    const ShaderCompileStatistics& ShaderCompiler::lastCompileStatistics() const
    {
        return m_Impl->statistics;
    }

    ShaderProgram ShaderCompiler::compile(const std::filesystem::path& file, const ShaderCompileOptions& options)
    {
        auto program = compileExpected(file, options);
        if (!program)
        {
            throw std::runtime_error(program.error()); // Legacy compile boundary.
        }
        return std::move(*program);
    }

    ShaderProgram ShaderCompiler::compileSource(const std::filesystem::path& file,
                                                std::string_view             text,
                                                const ShaderCompileOptions&  options)
    {
        auto program = compileSourceExpected(file, text, options);
        if (!program)
        {
            throw std::runtime_error(program.error()); // Legacy compile boundary.
        }
        return std::move(*program);
    }

    std::expected<ShaderProgram, std::string> ShaderCompiler::compileExpected(const std::filesystem::path& file,
                                                                              const ShaderCompileOptions&  options)
    {
        m_Impl->statistics = {};
        auto source        = detail::shaderAbsolutePathExpected(file);
        if (!source)
        {
            return std::unexpected(source.error());
        }
        auto text = detail::readShaderTextExpected(*source);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        return compileSourceExpected(*source, *text, options);
    }

    std::expected<ShaderProgram, std::string>
    ShaderCompiler::compileSourceExpected(const std::filesystem::path& sourcePath,
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
        const bool wgsl      = program.profile == "wgsl";
        if (wgsl && rayQuery)
        {
            return std::unexpected("WGSL does not support ray-query shaders");
        }
        if (sourceText.find(char(0)) != std::string_view::npos)
        {
            return std::unexpected("Shader source contains a NUL character: " + sourcePath.string());
        }
        if (sourcePath.empty() || compileOptions.optimization < ShaderOptimization::eNone ||
            compileOptions.optimization > ShaderOptimization::eMaximal ||
            (compileOptions.debugInformation && (*compileOptions.debugInformation < ShaderDebugInformation::eNone ||
                                                 *compileOptions.debugInformation > ShaderDebugInformation::eMaximal)))
        {
            return std::unexpected("Invalid shader identity, optimization or debug mode");
        }
        const auto nul = [](const std::string& text)
        {
            return text.find(char(0)) != std::string::npos;
        };
        if (nul(compileOptions.profile) || std::ranges::any_of(compileOptions.capabilities, nul) ||
            std::ranges::any_of(compileOptions.entries,
                                [&](const auto& entry)
                                {
                                    return nul(entry.name);
                                }) ||
            std::ranges::any_of(compileOptions.defines,
                                [&](const auto& value)
                                {
                                    return nul(value.name) || nul(value.value);
                                }) ||
            std::ranges::any_of(compileOptions.linkSources,
                                [&](const auto& value)
                                {
                                    return nul(value.name) || nul(value.value);
                                }))
        {
            return std::unexpected("Shader option contains a NUL character");
        }
        auto requestedSignature = detail::shaderCompileSignatureExpected(sourcePath, sourceText, compileOptions);
        if (!requestedSignature)
        {
            return std::unexpected(requestedSignature.error());
        }
        const auto signature = std::move(*requestedSignature);
        program.compileKey   = std::to_string(XXH3_64bits(signature.data(), signature.size()));
        const auto cacheFile = m_Impl->cacheDirectory.empty() ?
                                   std::filesystem::path {} :
                                   m_Impl->cacheDirectory / (program.compileKey + ".vshadercache");
        if (!cacheFile.empty())
        {
            const auto cachedProgram = [&]() -> std::expected<std::optional<ShaderProgram>, std::string>
            {
                auto exists = detail::shaderRegularFileExpected(cacheFile);
                if (!exists)
                {
                    return std::unexpected(exists.error());
                }
                if (!*exists)
                {
                    return std::optional<ShaderProgram> {};
                }
                auto document = detail::readShaderArchiveExpected(cacheFile);
                if (!document)
                {
                    return std::unexpected(document.error());
                }
                const auto kind        = document->find("kind");
                const auto request     = document->find("request");
                const auto code        = document->find("program");
                const auto diagnostics = document->find("diagnostics");
                if (kind == document->end() || *kind != "development_shader" || request == document->end() ||
                    !request->is_string() || code == document->end() || diagnostics == document->end() ||
                    !diagnostics->is_string())
                {
                    return std::unexpected("Unsupported development shader cache");
                }
                // A hash selects a file; the complete request establishes equality.
                if (request->get_ref<const std::string&>() != signature)
                {
                    return std::optional<ShaderProgram> {};
                }
                auto cached = detail::decodeShaderProgramExpected(*code);
                if (!cached)
                {
                    return std::unexpected(cached.error());
                }
                auto current =
                    detail::shaderDependenciesCurrentExpected(cached->dependencies, sourcePath, compileOptions);
                if (!current)
                {
                    return std::unexpected(current.error());
                }
                if (cached->compileKey != program.compileKey || !*current)
                {
                    return std::optional<ShaderProgram> {};
                }
                cached->diagnostics = diagnostics->get_ref<const std::string&>();
                return std::optional<ShaderProgram> {std::move(*cached)};
            }();
            if (!cachedProgram)
            {
                Logger::core().warn("Rebuilding unreadable development shader cache {}: {}",
                                    cacheFile.string(),
                                    cachedProgram.error());
            }
            else if (*cachedProgram)
            {
                statistics.programCacheHit   = true;
                statistics.totalMilliseconds = milliseconds(started);
                Logger::core().info("Shader program cache hit: {} ({:.1f} ms)",
                                    sourcePath.string(),
                                    statistics.totalMilliseconds);
                return std::move(**cachedProgram);
            }
        }
        const auto frontendStarted = Clock::now();
        auto       resolvedSource  = detail::shaderAbsolutePathExpected(sourcePath);
        if (!resolvedSource)
        {
            return std::unexpected(resolvedSource.error());
        }
        const auto source   = std::move(*resolvedSource);
        auto       diagnose = [&](SlangResult                 result,
                            const ComPtr<slang::IBlob>& blob,
                            const char*                 operation) -> std::expected<void, std::string>
        {
            if (blob)
            {
                program.diagnostics += static_cast<const char*>(blob->getBufferPointer());
            }
            if (SLANG_FAILED(result))
            {
                return std::unexpected(source.string() + ": " + operation + "\n" + program.diagnostics);
            }
            return {};
        };
        auto& global = m_Impl->global;
        if (!global)
        {
            if (auto diagnosed =
                    diagnose(slang::createGlobalSession(global.writeRef()), {}, "Create Slang global session");
                !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }
        }
        slang::TargetDesc target {};
        target.format  = wgsl ? SLANG_WGSL : SLANG_SPIRV;
        target.profile = wgsl ? SLANG_PROFILE_UNKNOWN : global->findProfile(program.profile.c_str());
        if (!wgsl && (!program.profile.starts_with("spirv_") || target.profile == SLANG_PROFILE_UNKNOWN))
        {
            return std::unexpected("Unsupported SPIR-V profile: " + program.profile);
        }
        std::vector<slang::CompilerOptionEntry> options;
        if (!wgsl)
        {
            for (const auto name :
                 {slang::CompilerOptionName::VulkanUseEntryPointName, slang::CompilerOptionName::EmitSpirvDirectly})
            {
                slang::CompilerOptionEntry option {};
                option.name            = name;
                option.value.kind      = slang::CompilerOptionValueKind::Int;
                option.value.intValue0 = 1;
                options.push_back(option);
            }
        }
        static_assert(int(ShaderOptimization::eNone) == SLANG_OPTIMIZATION_LEVEL_NONE);
        static_assert(int(ShaderOptimization::eDefault) == SLANG_OPTIMIZATION_LEVEL_DEFAULT);
        static_assert(int(ShaderOptimization::eHigh) == SLANG_OPTIMIZATION_LEVEL_HIGH);
        static_assert(int(ShaderOptimization::eMaximal) == SLANG_OPTIMIZATION_LEVEL_MAXIMAL);
        static_assert(int(ShaderDebugInformation::eNone) == SLANG_DEBUG_INFO_LEVEL_NONE);
        static_assert(int(ShaderDebugInformation::eMinimal) == SLANG_DEBUG_INFO_LEVEL_MINIMAL);
        static_assert(int(ShaderDebugInformation::eStandard) == SLANG_DEBUG_INFO_LEVEL_STANDARD);
        static_assert(int(ShaderDebugInformation::eMaximal) == SLANG_DEBUG_INFO_LEVEL_MAXIMAL);
        slang::CompilerOptionEntry optimization {};
        optimization.name            = slang::CompilerOptionName::Optimization;
        optimization.value.kind      = slang::CompilerOptionValueKind::Int;
        optimization.value.intValue0 = int(compileOptions.optimization);
        options.push_back(optimization);
        slang::CompilerOptionEntry debugInfo {};
        debugInfo.name            = slang::CompilerOptionName::DebugInformation;
        debugInfo.value.kind      = slang::CompilerOptionValueKind::Int;
        debugInfo.value.intValue0 = int(detail::shaderDebugInformation(compileOptions));
        options.push_back(debugInfo);
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
                return std::unexpected("Unknown Slang capability: " + name);
            }
            options.push_back(capability);
        }
        std::vector<std::string> paths {source.parent_path().string()};
        for (const auto& directory : includeDirectories)
        {
            auto absolute = detail::shaderAbsolutePathExpected(directory);
            if (!absolute)
            {
                return std::unexpected(absolute.error());
            }
            paths.push_back(absolute->string());
        }
        for (const auto& path : compileOptions.linkModules)
        {
            const auto resolved = path.is_absolute() ? path : source.parent_path() / path;
            auto       absolute = detail::shaderAbsolutePathExpected(resolved);
            if (!absolute)
            {
                return std::unexpected(absolute.error());
            }
            paths.push_back(absolute->parent_path().string());
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
        auto primaryExists = detail::shaderRegularFileExpected(source);
        if (!primaryExists)
        {
            return std::unexpected(primaryExists.error());
        }
        if (*primaryExists)
        {
            ComPtr<ISlangBlob> primary;
            if (auto diagnosed = diagnose(fileSystem->loadFile(source.string().c_str(), primary.writeRef()),
                                          {},
                                          "Snapshot primary shader source");
                !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }
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
        if (auto diagnosed = diagnose(global->createSession(desc, session.writeRef()), {}, "Create Slang session");
            !diagnosed)
        {
            return std::unexpected(diagnosed.error());
        }
        ComPtr<slang::IBlob> diagnostics;
        const std::string    sourceBuffer(sourceText);
        auto                 frontendOptions = compileOptions;
        frontendOptions.runSpirvToolsOptimization = true;
        frontendOptions.entries.clear();
        frontendOptions.linkSources.clear();
        frontendOptions.typeConformances.clear();
        for (const auto& path : compileOptions.linkModules)
        {
            const auto resolved = path.is_absolute() ? path : source.parent_path() / path;
            auto       absolute = detail::shaderAbsolutePathExpected(resolved);
            if (!absolute)
            {
                return std::unexpected(absolute.error());
            }
            frontendOptions.includeDirectories.push_back(absolute->parent_path());
        }
        frontendOptions.linkModules.clear();
        auto frontendRequest = detail::shaderCompileSignatureExpected(source, sourceText, frontendOptions);
        if (!frontendRequest)
        {
            return std::unexpected(frontendRequest.error());
        }
        const auto frontendSignature = std::move(*frontendRequest);
        const auto previous          = std::ranges::find(m_Impl->modules, frontendSignature, &Impl::Module::signature);
        std::vector<ShaderDependency> moduleDependencies;
        slang::IModule*               module = nullptr;
        ComPtr<ISlangBlob>            moduleCode;
        std::string                   moduleDiagnostics;
        bool                          reuseModule = false;
        if (previous != m_Impl->modules.end())
        {
            auto current = detail::shaderDependenciesCurrentExpected(previous->dependencies, source, frontendOptions);
            if (!current)
            {
                return std::unexpected(current.error());
            }
            reuseModule = *current;
        }
        if (reuseModule)
        {
            module = session->loadModuleFromIRBlob(source.stem().string().c_str(),
                                                   source.string().c_str(),
                                                   previous->code,
                                                   diagnostics.writeRef());
            if (auto diagnosed = diagnose(module ? SLANG_OK : SLANG_FAIL, diagnostics, "Load checked Slang module");
                !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }
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
            if (auto diagnosed = diagnose(module ? SLANG_OK : SLANG_FAIL, diagnostics, "Load shader module");
                !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }
            moduleDependencies = fileSystem->dependencies();
            moduleDiagnostics  = program.diagnostics;
            if (auto diagnosed =
                    diagnose(module->serialize(moduleCode.writeRef()), {}, "Serialize checked Slang module");
                !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }
        }
        std::vector<ComPtr<slang::IEntryPoint>> entries;
        if (requested.empty())
        {
            entries.resize(size_t(module->getDefinedEntryPointCount()));
            for (size_t i = 0; i < entries.size(); ++i)
            {
                if (auto diagnosed = diagnose(module->getDefinedEntryPoint(SlangInt32(i), entries[i].writeRef()),
                                              {},
                                              "Discover shader entry point");
                    !diagnosed)
                {
                    return std::unexpected(diagnosed.error());
                }
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
                    const auto mapped = shaderStage(candidate);
                    if (mapped && *mapped == requested[i].stage)
                    {
                        stage = candidate;
                        break;
                    }
                }
                if (stage == SLANG_STAGE_NONE)
                {
                    return std::unexpected("Unsupported shader stage request: " + requested[i].name);
                }
                diagnostics.setNull();
                if (auto diagnosed = diagnose(module->findAndCheckEntryPoint(requested[i].name.c_str(),
                                                                             stage,
                                                                             entries[i].writeRef(),
                                                                             diagnostics.writeRef()),
                                              diagnostics,
                                              "Find shader entry point");
                    !diagnosed)
                {
                    return std::unexpected(diagnosed.error());
                }
            }
        }
        if (entries.empty())
        {
            return std::unexpected("No annotated shader entries: " + source.string());
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
            if (auto diagnosed = diagnose(fileSystem->loadFile(resolved.string().c_str(), linkedBytes.writeRef()),
                                          {},
                                          "Snapshot linked Slang module");
                !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }
            const std::string linkedSource(static_cast<const char*>(linkedBytes->getBufferPointer()),
                                           linkedBytes->getBufferSize());
            diagnostics.setNull();
            auto* linkedModule = session->loadModuleFromSourceString(resolved.stem().string().c_str(),
                                                                     resolved.string().c_str(),
                                                                     linkedSource.c_str(),
                                                                     diagnostics.writeRef());
            if (auto diagnosed =
                    diagnose(linkedModule ? SLANG_OK : SLANG_FAIL, diagnostics, "Load linked Slang module");
                !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }
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
            if (auto diagnosed =
                    diagnose(linkedModule ? SLANG_OK : SLANG_FAIL, diagnostics, "Load link-time Slang source");
                !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }
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
        if (auto diagnosed = diagnose(result, diagnostics, "Compose shader"); !diagnosed)
        {
            return std::unexpected(diagnosed.error());
        }
        if (!compileOptions.typeConformances.empty())
        {
            diagnostics.setNull();
            auto* types = composed->getLayout(0, diagnostics.writeRef());
            if (auto diagnosed = diagnose(types ? SLANG_OK : SLANG_FAIL, diagnostics, "Reflect conformance types");
                !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }

            struct SelectedType
            {
                slang::TypeReflection* type;
                slang::TypeReflection* interfaceType;
                uint32_t               id;
            };

            std::vector<SelectedType>                    selected;
            std::vector<ComPtr<slang::ITypeConformance>> owners;
            std::vector<slang::IComponentType*>          specialized {composed};
            for (const auto& conformance : compileOptions.typeConformances)
            {
                auto*      type          = types->findTypeByName(conformance.typeName.c_str());
                auto*      interfaceType = types->findTypeByName(conformance.interfaceType.c_str());
                const auto identity      = conformance.typeName + " : " + conformance.interfaceType;
                if (!type || !interfaceType)
                {
                    return std::unexpected("Unknown shader type/interface in conformance: " + identity);
                }
                if (interfaceType->getKind() != slang::TypeReflection::Kind::Interface)
                {
                    return std::unexpected("Shader conformance target is not an interface: " + identity);
                }
                // Aliases must not assign two IDs to one witness or share an ID within an interface.
                if (std::ranges::any_of(selected,
                                        [&](const auto& previous)
                                        {
                                            return previous.interfaceType == interfaceType &&
                                                   (previous.type == type || previous.id == conformance.id);
                                        }))
                {
                    return std::unexpected("Duplicate resolved implementation or dispatch ID: " + identity);
                }
                ComPtr<slang::ITypeConformance> owner;
                diagnostics.setNull();
                result               = session->createTypeConformanceComponentType(type,
                                                                     interfaceType,
                                                                     owner.writeRef(),
                                                                     SlangInt(conformance.id),
                                                                     diagnostics.writeRef());
                const auto operation = "Create shader type conformance " + identity;
                if (auto diagnosed = diagnose(result, diagnostics, operation.c_str()); !diagnosed)
                {
                    return std::unexpected(diagnosed.error());
                }
                selected.push_back({type, interfaceType, conformance.id});
                specialized.push_back(owner);
                owners.push_back(std::move(owner));
            }
            ComPtr<slang::IComponentType> candidate;
            diagnostics.setNull();
            result = session->createCompositeComponentType(specialized.data(),
                                                           SlangInt(specialized.size()),
                                                           candidate.writeRef(),
                                                           diagnostics.writeRef());
            if (auto diagnosed = diagnose(result, diagnostics, "Compose shader type conformances"); !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }
            composed = std::move(candidate);
        }
        diagnostics.setNull();
        result = composed->link(linked.writeRef(), diagnostics.writeRef());
        if (auto diagnosed = diagnose(result, diagnostics, "Link shader"); !diagnosed)
        {
            return std::unexpected(diagnosed.error());
        }
        diagnostics.setNull();
        auto* layout = linked->getLayout(0, diagnostics.writeRef());
        if (auto diagnosed = diagnose(layout ? SLANG_OK : SLANG_FAIL, diagnostics, "Reflect shader entries");
            !diagnosed)
        {
            return std::unexpected(diagnosed.error());
        }
        statistics.linkMilliseconds = milliseconds(linkStarted);
        const auto codegenStarted   = Clock::now();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto*       reflection = layout->getEntryPointByIndex(SlangUInt(i));
            auto        stage      = shaderStage(reflection->getStage());
            if (!stage)
            {
                return std::unexpected(stage.error());
            }
            ShaderEntry entry {reflection->getName(), *stage};
            if (!requested.empty() && entry.stage != requested[i].stage)
            {
                return std::unexpected("Shader stage does not match declaration: " + entry.name);
            }
            ComPtr<slang::IBlob> code;
            diagnostics.setNull();
            result = linked->getEntryPointCode(SlangInt(i), 0, code.writeRef(), diagnostics.writeRef());
            if (auto diagnosed = diagnose(result, diagnostics, wgsl ? "Emit WGSL" : "Emit SPIR-V"); !diagnosed)
            {
                return std::unexpected(diagnosed.error());
            }
            if (!code || code->getBufferSize() == 0)
            {
                return std::unexpected("Slang emitted empty shader code: " + entry.name);
            }
            CompiledShader shader;
            shader.entry = std::move(entry);
            if (wgsl)
            {
                shader.wgsl.assign(static_cast<const char*>(code->getBufferPointer()), code->getBufferSize());
            }
            else
            {
                if (code->getBufferSize() % sizeof(uint32_t) != 0)
                {
                    return std::unexpected("Slang emitted misaligned SPIR-V: " + shader.entry.name);
                }
                shader.words.resize(code->getBufferSize() / sizeof(uint32_t));
                std::memcpy(shader.words.data(), code->getBufferPointer(), code->getBufferSize());
                if (compileOptions.runSpirvToolsOptimization &&
                    compileOptions.optimization != ShaderOptimization::eNone)
                {
                    auto optimized = optimizeSpirvExpected(shader.words, shader.entry.name, program.diagnostics);
                    if (!optimized)
                    {
                        return std::unexpected(source.string() + ": " + optimized.error());
                    }
                    shader.words = std::move(*optimized);
                }
            }
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
            program.shaders.push_back(std::move(shader));
        }
        auto parameters = detail::reflectShaderParametersExpected(layout);
        if (!parameters)
        {
            return std::unexpected(parameters.error());
        }
        program.parameters             = std::move(*parameters);
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
                return std::unexpected("Shader input changed between frontend and link: " + dependency.path.string());
            }
        }
        auto dependenciesCurrent =
            detail::shaderDependenciesCurrentExpected(program.dependencies, source, compileOptions);
        if (!dependenciesCurrent)
        {
            return std::unexpected(dependenciesCurrent.error());
        }
        if (!*dependenciesCurrent)
        {
            return std::unexpected("Shader dependency changed while compiling: " + source.string());
        }
        auto finalRequest = detail::shaderCompileSignatureExpected(sourcePath, sourceText, compileOptions);
        if (!finalRequest)
        {
            return std::unexpected(finalRequest.error());
        }
        if (*finalRequest != signature)
        {
            return std::unexpected("Linked shader input changed while compiling: " + source.string());
        }
        if (auto valid = detail::validateShaderProgramExpected(program); !valid)
        {
            return std::unexpected(valid.error());
        }
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
            auto written = detail::writeShaderArchiveExpected(cacheFile,
                                                              {{"version", 1},
                                                               {"kind", "development_shader"},
                                                               {"request", signature},
                                                               {"diagnostics", program.diagnostics},
                                                               {"program", detail::encodeShaderProgram(program)}});
            if (!written)
            {
                Logger::core().warn("Could not write development shader cache {}: {}",
                                    cacheFile.string(),
                                    written.error());
            }
        }
        statistics.totalMilliseconds = milliseconds(started);
        Logger::core().info(
            "Shader compiled: {} | frontend {:.1f} ms{}, link {:.1f} ms, codegen {:.1f} ms, total {:.1f} ms",
            source.string(),
            statistics.frontendMilliseconds,
            statistics.moduleCacheHit ? " (IR reused)" : "",
            statistics.linkMilliseconds,
            statistics.codegenMilliseconds,
            statistics.totalMilliseconds);
        return program;
    }

} // namespace vultra
