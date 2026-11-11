#include "shader_archive.hpp"
#include "shader_cache.hpp"
#include "shader_file_system.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/shader/shader_compiler.hpp>

#include <slang-com-ptr.h>
#include <slang.h>
#include <spirv-tools/optimizer.hpp>
#include <xxhash.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <optional>

namespace vultra
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double milliseconds(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        // The static Slang build carries no SPIRV-Opt module, so its optimization level has no effect on SPIR-V.
        // Stage interface variables are preserved: a stage is optimized without seeing the stages it links to.
        std::expected<std::vector<uint32_t>, Error> optimizeSpirv(const std::vector<uint32_t>& words,
                                                                  const std::string&           entry)
        {
            std::string         messages;
            spvtools::Optimizer optimizer(SPV_ENV_VULKAN_1_3);
            optimizer.SetMessageConsumer(
                [&messages](spv_message_level_t, const char*, const spv_position_t&, const char* message)
                {
                    messages += message;
                    messages += '\n';
                });
            optimizer.RegisterPerformancePasses(true);
            std::vector<uint32_t> optimized;
            if (!optimizer.Run(words.data(), words.size(), &optimized))
            {
                return makeError("Optimize SPIR-V of {}\n{}", entry, messages);
            }
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

        std::expected<VriShaderStageBits, Error> shaderStage(SlangStage stage)
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
                    return makeError("Unsupported cooked shader stage");
            }
        }

        std::expected<std::string, Error> readSource(const std::filesystem::path& file)
        {
            std::ifstream input(file, std::ios::binary);
            if (!input)
            {
                return makeError("Open Slang source: {}", file.string());
            }
            return std::string {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }

        // The cached program for this exact request, or nothing when the file holds a different request.
        std::expected<std::optional<ShaderProgram>, Error> readDevelopmentCache(const std::filesystem::path& file,
                                                                                const std::string&           signature)
        {
            const auto document = detail::readShaderArchive(file);
            if (!document)
            {
                return std::unexpected(document.error());
            }
            const auto request     = document->find("request");
            const auto program     = document->find("program");
            const auto diagnostics = document->find("diagnostics");
            if (document->at("kind") != "development_shader" || request == document->end() ||
                program == document->end() || diagnostics == document->end() || !diagnostics->is_string())
            {
                return makeError("Unsupported development shader cache");
            }
            // A hash selects a file; the complete request establishes equality.
            if (*request != signature)
            {
                return std::nullopt;
            }
            auto cached = detail::decodeShaderProgram(*program);
            if (!cached)
            {
                return std::unexpected(cached.error());
            }
            if (auto valid = detail::validateShaderProgram(*cached); !valid)
            {
                return std::unexpected(valid.error());
            }
            cached->diagnostics = diagnostics->get<std::string>();
            return std::move(*cached);
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
        m_Impl->cacheDirectory = std::move(cacheDirectory);
    }

    std::expected<ShaderCompiler, Error> ShaderCompiler::create(const std::filesystem::path& cacheDirectory)
    {
        if (cacheDirectory.empty())
        {
            return ShaderCompiler(std::filesystem::path {});
        }
        const auto directory = detail::absolutePath(cacheDirectory);
        if (!directory)
        {
            return std::unexpected(directory.error());
        }
        return ShaderCompiler(directory->lexically_normal());
    }

    ShaderCompiler::~ShaderCompiler()                                    = default;
    ShaderCompiler::ShaderCompiler(ShaderCompiler&&) noexcept            = default;
    ShaderCompiler& ShaderCompiler::operator=(ShaderCompiler&&) noexcept = default;

    const ShaderCompileStatistics& ShaderCompiler::lastCompileStatistics() const
    {
        return m_Impl->statistics;
    }

    std::expected<ShaderProgram, Error> ShaderCompiler::compile(const std::filesystem::path& file,
                                                                const ShaderCompileOptions&  options)
    {
        const auto text = readSource(file);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        return compileSource(file, *text, options);
    }

    std::expected<ShaderProgram, Error> ShaderCompiler::compileSource(const std::filesystem::path& sourcePath,
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
            return makeError("WGSL does not support ray-query shaders");
        }
        if (sourceText.find(char(0)) != std::string_view::npos)
        {
            return makeError("Shader source contains a NUL character: {}", sourcePath.string());
        }
        const auto signature = detail::shaderCompileSignature(sourcePath, sourceText, compileOptions);
        if (!signature)
        {
            return std::unexpected(signature.error());
        }
        program.compileKey        = std::to_string(XXH3_64bits(signature->data(), signature->size()));
        const auto      cacheFile = m_Impl->cacheDirectory.empty() ?
                                        std::filesystem::path {} :
                                        m_Impl->cacheDirectory / (program.compileKey + ".vshadercache");
        std::error_code cacheStatus;
        if (!cacheFile.empty() && std::filesystem::is_regular_file(cacheFile, cacheStatus))
        {
            auto                       cached  = readDevelopmentCache(cacheFile, *signature);
            std::expected<bool, Error> current = false;
            if (cached && *cached && (*cached)->compileKey == program.compileKey)
            {
                current = detail::shaderDependenciesCurrent((*cached)->dependencies, sourcePath, compileOptions);
            }
            if (!cached || !current)
            {
                Logger::core().warn("Rebuilding unreadable development shader cache {}: {}",
                                    cacheFile.string(),
                                    cached ? current.error().message : cached.error().message);
            }
            else if (*current)
            {
                statistics.programCacheHit   = true;
                statistics.totalMilliseconds = milliseconds(started);
                Logger::core().info("Shader program cache hit: {} ({:.1f} ms)",
                                    sourcePath.string(),
                                    statistics.totalMilliseconds);
                return std::move(**cached);
            }
        }
        const auto frontendStarted = Clock::now();
        const auto absoluteSource  = detail::absolutePath(sourcePath);
        if (!absoluteSource)
        {
            return std::unexpected(absoluteSource.error());
        }
        const auto source   = absoluteSource->lexically_normal();
        auto       diagnose = [&](SlangResult                 result,
                                  const ComPtr<slang::IBlob>& blob,
                                  const char*                 operation) -> std::expected<void, Error>
        {
            if (blob)
            {
                program.diagnostics += static_cast<const char*>(blob->getBufferPointer());
            }
            if (SLANG_FAILED(result))
            {
                return makeError("{}: {}\n{}", source.string(), operation, program.diagnostics);
            }
            return {};
        };
        auto& global = m_Impl->global;
        if (!global)
        {
            if (auto created =
                    diagnose(slang::createGlobalSession(global.writeRef()), {}, "Create Slang global session");
                !created)
            {
                return std::unexpected(created.error());
            }
        }
        slang::TargetDesc target {};
        target.format  = wgsl ? SLANG_WGSL : SLANG_SPIRV;
        target.profile = wgsl ? SLANG_PROFILE_UNKNOWN : global->findProfile(program.profile.c_str());
        if (!wgsl && (!program.profile.starts_with("spirv_") || target.profile == SLANG_PROFILE_UNKNOWN))
        {
            return makeError("Unsupported SPIR-V profile: {}", program.profile);
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
        slang::CompilerOptionEntry debugInfo {};
        debugInfo.name       = slang::CompilerOptionName::DebugInformation;
        debugInfo.value.kind = slang::CompilerOptionValueKind::Int;
        debugInfo.value.intValue0 =
            compileOptions.debugInfo ? SLANG_DEBUG_INFO_LEVEL_STANDARD : SLANG_DEBUG_INFO_LEVEL_NONE;
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
                return makeError("Unknown Slang capability: {}", name);
            }
            options.push_back(capability);
        }
        std::vector<std::string> paths {source.parent_path().string()};
        for (const auto& directory : includeDirectories)
        {
            const auto resolved = detail::absolutePath(directory);
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            paths.push_back(resolved->lexically_normal().string());
        }
        std::vector<std::filesystem::path> moduleDirectories;
        for (const auto& path : compileOptions.linkModules)
        {
            const auto resolved = detail::absolutePath(path.is_absolute() ? path : source.parent_path() / path);
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            moduleDirectories.push_back(resolved->parent_path());
            paths.push_back(moduleDirectories.back().string());
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
        std::error_code sourceStatus;
        if (std::filesystem::is_regular_file(source, sourceStatus))
        {
            ComPtr<ISlangBlob> primary;
            if (auto loaded = diagnose(fileSystem->loadFile(source.string().c_str(), primary.writeRef()),
                                       {},
                                       "Snapshot primary shader source");
                !loaded)
            {
                return std::unexpected(loaded.error());
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
        if (auto created = diagnose(global->createSession(desc, session.writeRef()), {}, "Create Slang session");
            !created)
        {
            return std::unexpected(created.error());
        }
        ComPtr<slang::IBlob> diagnostics;
        const std::string    sourceBuffer(sourceText);
        auto                 frontendOptions = compileOptions;
        frontendOptions.entries.clear();
        frontendOptions.linkSources.clear();
        frontendOptions.includeDirectories.insert(frontendOptions.includeDirectories.end(),
                                                  moduleDirectories.begin(),
                                                  moduleDirectories.end());
        frontendOptions.linkModules.clear();
        const auto frontendSignature = detail::shaderCompileSignature(source, sourceText, frontendOptions);
        if (!frontendSignature)
        {
            return std::unexpected(frontendSignature.error());
        }
        const auto previous = std::ranges::find(m_Impl->modules, *frontendSignature, &Impl::Module::signature);
        std::expected<bool, Error> previousCurrent = false;
        if (previous != m_Impl->modules.end())
        {
            previousCurrent = detail::shaderDependenciesCurrent(previous->dependencies, source, frontendOptions);
            if (!previousCurrent)
            {
                return std::unexpected(previousCurrent.error());
            }
        }
        std::vector<ShaderDependency> moduleDependencies;
        slang::IModule*               module = nullptr;
        ComPtr<ISlangBlob>            moduleCode;
        std::string                   moduleDiagnostics;
        if (*previousCurrent)
        {
            module = session->loadModuleFromIRBlob(source.stem().string().c_str(),
                                                   source.string().c_str(),
                                                   previous->code,
                                                   diagnostics.writeRef());
            if (auto loaded = diagnose(module ? SLANG_OK : SLANG_FAIL, diagnostics, "Load checked Slang module");
                !loaded)
            {
                return std::unexpected(loaded.error());
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
            if (auto loaded = diagnose(module ? SLANG_OK : SLANG_FAIL, diagnostics, "Load shader module"); !loaded)
            {
                return std::unexpected(loaded.error());
            }
            moduleDependencies = fileSystem->dependencies();
            moduleDiagnostics  = program.diagnostics;
            if (auto serialized =
                    diagnose(module->serialize(moduleCode.writeRef()), {}, "Serialize checked Slang module");
                !serialized)
            {
                return std::unexpected(serialized.error());
            }
        }
        std::vector<ComPtr<slang::IEntryPoint>> entries;
        if (requested.empty())
        {
            entries.resize(size_t(module->getDefinedEntryPointCount()));
            for (size_t i = 0; i < entries.size(); ++i)
            {
                if (auto found = diagnose(module->getDefinedEntryPoint(SlangInt32(i), entries[i].writeRef()),
                                          {},
                                          "Discover shader entry point");
                    !found)
                {
                    return std::unexpected(found.error());
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
                    if (shaderStage(candidate) == requested[i].stage)
                    {
                        stage = candidate;
                        break;
                    }
                }
                diagnostics.setNull();
                if (auto found = diagnose(module->findAndCheckEntryPoint(requested[i].name.c_str(),
                                                                         stage,
                                                                         entries[i].writeRef(),
                                                                         diagnostics.writeRef()),
                                          diagnostics,
                                          "Find shader entry point");
                    !found)
                {
                    return std::unexpected(found.error());
                }
            }
        }
        if (entries.empty())
        {
            return makeError("No annotated shader entries: {}", source.string());
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
            if (auto loaded = diagnose(fileSystem->loadFile(resolved.string().c_str(), linkedBytes.writeRef()),
                                       {},
                                       "Snapshot linked Slang module");
                !loaded)
            {
                return std::unexpected(loaded.error());
            }
            const std::string linkedSource(static_cast<const char*>(linkedBytes->getBufferPointer()),
                                           linkedBytes->getBufferSize());
            diagnostics.setNull();
            auto* linkedModule = session->loadModuleFromSourceString(resolved.stem().string().c_str(),
                                                                     resolved.string().c_str(),
                                                                     linkedSource.c_str(),
                                                                     diagnostics.writeRef());
            if (auto loaded = diagnose(linkedModule ? SLANG_OK : SLANG_FAIL, diagnostics, "Load linked Slang module");
                !loaded)
            {
                return std::unexpected(loaded.error());
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
            if (auto loaded =
                    diagnose(linkedModule ? SLANG_OK : SLANG_FAIL, diagnostics, "Load link-time Slang source");
                !loaded)
            {
                return std::unexpected(loaded.error());
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
        if (auto composedResult = diagnose(result, diagnostics, "Compose shader"); !composedResult)
        {
            return std::unexpected(composedResult.error());
        }
        diagnostics.setNull();
        result = composed->link(linked.writeRef(), diagnostics.writeRef());
        if (auto linkedResult = diagnose(result, diagnostics, "Link shader"); !linkedResult)
        {
            return std::unexpected(linkedResult.error());
        }
        diagnostics.setNull();
        auto* layout = linked->getLayout(0, diagnostics.writeRef());
        if (auto reflected = diagnose(layout ? SLANG_OK : SLANG_FAIL, diagnostics, "Reflect shader entries");
            !reflected)
        {
            return std::unexpected(reflected.error());
        }
        statistics.linkMilliseconds = milliseconds(linkStarted);
        const auto codegenStarted   = Clock::now();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto*      reflection = layout->getEntryPointByIndex(SlangUInt(i));
            const auto stage      = shaderStage(reflection->getStage());
            if (!stage)
            {
                return std::unexpected(stage.error());
            }
            ShaderEntry entry {reflection->getName(), *stage};
            if (!requested.empty() && entry.stage != requested[i].stage)
            {
                return makeError("Shader stage does not match declaration: {}", entry.name);
            }
            ComPtr<slang::IBlob> code;
            diagnostics.setNull();
            result = linked->getEntryPointCode(SlangInt(i), 0, code.writeRef(), diagnostics.writeRef());
            if (auto emitted = diagnose(result, diagnostics, wgsl ? "Emit WGSL" : "Emit SPIR-V"); !emitted)
            {
                return std::unexpected(emitted.error());
            }
            if (!code || code->getBufferSize() == 0)
            {
                return makeError("Slang emitted empty shader code: {}", entry.name);
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
                    return makeError("Slang emitted misaligned SPIR-V: {}", shader.entry.name);
                }
                shader.words.resize(code->getBufferSize() / sizeof(uint32_t));
                std::memcpy(shader.words.data(), code->getBufferPointer(), code->getBufferSize());
                if (compileOptions.optimize)
                {
                    auto optimized = optimizeSpirv(shader.words, shader.entry.name);
                    if (!optimized)
                    {
                        return std::unexpected(optimized.error());
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
            auto attributes = detail::reflectShaderAttributes(reflection->getFunction());
            if (!attributes)
            {
                return std::unexpected(attributes.error());
            }
            shader.attributes = std::move(*attributes);
            program.shaders.push_back(std::move(shader));
        }
        auto parameters = detail::reflectShaderParameters(layout->getGlobalParamsVarLayout());
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
                return makeError("Shader input changed between frontend and link: {}", dependency.path.string());
            }
        }
        const auto current = detail::shaderDependenciesCurrent(program.dependencies, source, compileOptions);
        if (!current)
        {
            return std::unexpected(current.error());
        }
        if (!*current)
        {
            return makeError("Shader dependency changed while compiling: {}", source.string());
        }
        const auto linkedSignature = detail::shaderCompileSignature(sourcePath, sourceText, compileOptions);
        if (!linkedSignature)
        {
            return std::unexpected(linkedSignature.error());
        }
        if (*linkedSignature != *signature)
        {
            return makeError("Linked shader input changed while compiling: {}", source.string());
        }
        if (auto valid = detail::validateShaderProgram(program); !valid)
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
            m_Impl->modules.push_back({*frontendSignature,
                                       std::move(moduleCode),
                                       std::move(moduleDependencies),
                                       std::move(moduleDiagnostics)});
        }
        if (!cacheFile.empty())
        {
            if (auto written = detail::writeShaderArchive(cacheFile,
                                                          {{"version", 1},
                                                           {"kind", "development_shader"},
                                                           {"request", *signature},
                                                           {"diagnostics", program.diagnostics},
                                                           {"program", detail::encodeShaderProgram(program)}});
                !written)
            {
                Logger::core().warn("Could not write development shader cache {}: {}",
                                    cacheFile.string(),
                                    written.error().message);
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
