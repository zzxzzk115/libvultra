#include "shader_archive.hpp"
#include "shader_cache.hpp"
#include "shader_file_system.hpp"
#include "shader_toolchain_key.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>
#include <slang-com-ptr.h>
#include <slang.h>
#include <xxhash.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        constexpr size_t kMaxFileSize = 64 * 1024 * 1024;

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
                default:
                    throw std::invalid_argument("Unsupported cooked shader stage");
            }
        }

        bool validStage(VriShaderStageBits stage)
        {
            return stage == VriShaderStage_Vertex || stage == VriShaderStage_Fragment ||
                   stage == VriShaderStage_Compute || stage == VriShaderStage_Geometry ||
                   stage == VriShaderStage_TessControl || stage == VriShaderStage_TessEval ||
                   stage == VriShaderStage_Task || stage == VriShaderStage_Mesh;
        }

        void validate(const ShaderProgram& program)
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
    } // namespace

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

    ShaderProgram ShaderProgram::compileSource(const std::filesystem::path& sourcePath,
                                               std::string_view             sourceText,
                                               const ShaderCompileOptions&  compileOptions)
    {
        using Slang::ComPtr;
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
        program.compileKey  = detail::shaderCompileKey(sourcePath, sourceText, compileOptions);
        const auto source   = std::filesystem::absolute(sourcePath).lexically_normal();
        auto       diagnose = [&](SlangResult result, const ComPtr<slang::IBlob>& blob, const char* operation)
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
        ComPtr<slang::IGlobalSession> global;
        diagnose(slang::createGlobalSession(global.writeRef()), {}, "Create Slang global session");
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
        // A new session discards cached modules on each source reload.
        ComPtr<slang::ISession> session;
        diagnose(global->createSession(desc, session.writeRef()), {}, "Create Slang session");
        ComPtr<slang::IBlob> diagnostics;
        const std::string    sourceBuffer(sourceText);
        auto*                module = session->loadModuleFromSourceString(source.stem().string().c_str(),
                                                           source.string().c_str(),
                                                           sourceBuffer.c_str(),
                                                           diagnostics.writeRef());
        diagnose(module ? SLANG_OK : SLANG_FAIL, diagnostics, "Load shader module");
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
                                             SLANG_STAGE_MESH})
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
            const auto    resolved = path.is_absolute() ? path : source.parent_path() / path;
            std::ifstream linkedInput(resolved, std::ios::binary);
            if (!linkedInput)
            {
                throw std::runtime_error("Open linked Slang module: " + resolved.string());
            }
            const std::string linkedSource {std::istreambuf_iterator<char>(linkedInput),
                                            std::istreambuf_iterator<char>()};
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
        program.parameters   = detail::reflectShaderParameters(layout->getGlobalParamsVarLayout());
        program.dependencies = fileSystem->dependencies();
        if (!detail::shaderDependenciesCurrent(program.dependencies, source, compileOptions))
        {
            throw std::runtime_error("Shader dependency changed while compiling: " + source.string());
        }
        validate(program);
        return program;
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
        Slang::ComPtr<slang::IGlobalSession> session;
        if (SLANG_FAILED(slang::createGlobalSession(session.writeRef())))
        {
            throw std::runtime_error("Create Slang session for compiler identity");
        }
        std::string identity = std::string(session->getBuildTagString()) + ";" + kShaderToolchainKey;
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
        validate(*this);
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
        validate(program);
        return program;
    }
} // namespace vultra
