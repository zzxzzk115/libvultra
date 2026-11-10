#include "shader_archive.hpp"

#include <vultra/drivers/rhi/shader_program.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
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

    std::expected<void, std::string>
    ShaderTypeConformance::validateExpected(std::span<const ShaderTypeConformance> conformances)
    {
        std::set<std::pair<std::string_view, std::string_view>> types;
        std::set<std::pair<std::string_view, uint32_t>>         ids;
        for (const auto& conformance : conformances)
        {
            if (conformance.typeName.empty() || conformance.interfaceType.empty() ||
                conformance.typeName.find(char(0)) != std::string::npos ||
                conformance.interfaceType.find(char(0)) != std::string::npos)
            {
                return std::unexpected("Type conformance requires nonempty type/interface names without NULs");
            }
            if (!types.emplace(conformance.interfaceType, conformance.typeName).second ||
                !ids.emplace(conformance.interfaceType, conformance.id).second)
            {
                return std::unexpected("Duplicate implementation or dispatch ID for interface: " +
                                       conformance.interfaceType);
            }
        }
        return {};
    }

    void detail::validateShaderProgram(const ShaderProgram& program, const VriDeviceDesc& device)
    {
        const auto result = validateShaderProgramExpected(program, device);
        if (!result)
            throw std::invalid_argument(result.error());
    }

    std::expected<void, std::string> detail::validateShaderProgramExpected(const ShaderProgram& program, const VriDeviceDesc& device)
    {
        // VRI's GL and Metal backends translate SPIR-V through SPIRV-Cross.
        const bool compatible = program.profile == "wgsl" ? device.graphicsAPI == VriGraphicsAPI_WebGPU :
                                                            (device.graphicsAPI == VriGraphicsAPI_Vulkan ||
                                                             device.graphicsAPI == VriGraphicsAPI_Metal ||
                                                             device.graphicsAPI == VriGraphicsAPI_OpenGL ||
                                                             device.graphicsAPI == VriGraphicsAPI_OpenGLES);
        if (!compatible)
        {
            return std::unexpected("Device backend cannot consume shader target: " + program.profile);
        }
        if (auto valid = validateShaderProgramExpected(program); !valid)
            return valid;
        if (program.rayQuery && !device.hasRayQuery)
        {
            return std::unexpected("Shader program requires ray query");
        }
        return {};
    }

    void detail::validateShaderProgram(const ShaderProgram& program)
    {
        const auto result = validateShaderProgramExpected(program);
        if (!result)
            throw std::invalid_argument(result.error());
    }

    std::expected<void, std::string> detail::validateShaderProgramExpected(const ShaderProgram& program)
    {
        const bool wgsl = program.profile == "wgsl";
        if ((!wgsl && !program.profile.starts_with("spirv_")) || program.profile.size() > 64 ||
            (wgsl && program.rayQuery))
        {
            return std::unexpected("Unsupported shader target: " + program.profile);
        }
        if (program.shaders.empty() || program.shaders.size() > 256)
        {
            return std::unexpected("Shader program needs between 1 and 256 entries");
        }
        std::set<std::string> names;
        for (const auto& shader : program.shaders)
        {
            const auto& name = shader.entry.name;
            if (name.empty() || name.size() > 4096 || name.find(char(0)) != std::string::npos ||
                !names.insert(name).second || !validStage(shader.entry.stage))
            {
                return std::unexpected("Invalid or duplicate shader entry: " + name);
            }
            if (wgsl)
            {
                if (!shader.words.empty() || shader.wgsl.empty() || shader.wgsl.size() > kMaxFileSize ||
                    shader.wgsl.find(char(0)) != std::string::npos ||
                    (shader.entry.stage != VriShaderStage_Vertex && shader.entry.stage != VriShaderStage_Fragment &&
                     shader.entry.stage != VriShaderStage_Compute))
                {
                    return std::unexpected("Invalid WGSL shader: " + name);
                }
            }
            else if (!shader.wgsl.empty() || shader.words.size() < 5 || shader.words.front() != 0x07230203 ||
                     shader.words.size() > kMaxFileSize / sizeof(uint32_t))
            {
                return std::unexpected("Invalid SPIR-V bytecode: " + name);
            }
        }
        return {};
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
            desc.stage = found->entry.stage;
            if (profile == "wgsl")
            {
                // VRI's WebGPU backend consumes NUL-terminated WGSL. The owned string provides that terminator.
                desc.bytecode     = found->wgsl.c_str();
                desc.bytecodeSize = found->wgsl.size() + 1;
            }
            else
            {
                desc.bytecode     = found->words.data();
                desc.bytecodeSize = found->words.size() * sizeof(uint32_t);
            }
            desc.entryPointName = found->entry.name.c_str();
            result.push_back(desc);
        }
        return result;
    }

    std::expected<std::vector<VriShaderDesc>, std::string>
    ShaderProgram::descriptorsExpected(std::span<const ShaderEntry> entries) const
    {
        if (entries.empty())
            return std::unexpected("Select at least one shader entry");
        std::vector<VriShaderDesc> result;
        result.reserve(entries.size());
        for (const auto& entry : entries)
        {
            const auto found = std::find_if(shaders.begin(), shaders.end(), [&](const CompiledShader& shader)
            {
                return shader.entry.name == entry.name && shader.entry.stage == entry.stage;
            });
            if (found == shaders.end())
                return std::unexpected("Shader program has no matching entry/stage: " + entry.name);
            VriShaderDesc desc {};
            desc.stage = found->entry.stage;
            if (profile == "wgsl")
            {
                desc.bytecode = found->wgsl.c_str();
                desc.bytecodeSize = found->wgsl.size() + 1;
            }
            else
            {
                desc.bytecode = found->words.data();
                desc.bytecodeSize = found->words.size() * sizeof(uint32_t);
            }
            desc.entryPointName = found->entry.name.c_str();
            result.push_back(desc);
        }
        return result;
    }

    std::expected<void, std::string> ShaderProgram::saveExpected(const std::filesystem::path& file) const
    {
        if (auto valid = detail::validateShaderProgramExpected(*this); !valid)
        {
            return valid;
        }
        auto document       = detail::encodeShaderProgram(*this);
        document["version"] = 1;
        document["kind"]    = "raw_slang";
        return detail::writeShaderArchiveExpected(file, document);
    }

    void ShaderProgram::save(const std::filesystem::path& file) const
    {
        if (auto saved = saveExpected(file); !saved)
        {
            throw std::runtime_error(saved.error());
        }
    }

    ShaderProgram ShaderProgram::load(const std::filesystem::path& file)
    {
        auto program = loadExpected(file);
        if (!program)
        {
            throw std::runtime_error(program.error());
        }
        return std::move(*program);
    }

    ShaderProgram ShaderProgram::load(std::span<const std::byte> bytes, const std::filesystem::path& identity)
    {
        auto program = loadExpected(bytes, identity);
        if (!program)
        {
            throw std::runtime_error(program.error());
        }
        return std::move(*program);
    }
} // namespace vultra
