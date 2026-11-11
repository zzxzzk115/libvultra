#include "shader_archive.hpp"

#include <vultra/shader/shader_program.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>

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

        std::expected<ShaderProgram, Error> programFromArchive(const nlohmann::json&        document,
                                                               const std::filesystem::path& identity)
        {
            // readShaderArchive has checked that the kind is present.
            if (document.at("kind") != "raw_slang")
            {
                return makeError("ShaderProgram requires a raw Slang artifact: {}", identity.string());
            }
            auto program = detail::decodeShaderProgram(document);
            if (!program)
            {
                return program;
            }
            if (auto valid = detail::validateShaderProgram(*program); !valid)
            {
                return std::unexpected(valid.error());
            }
            return program;
        }
    } // namespace

    std::expected<void, Error> detail::validateShaderProgram(const ShaderProgram& program, const VriDeviceDesc& device)
    {
        // VRI's GL backends consume SPIR-V through their own SPIRV-Cross path.
        const bool compatible = program.profile == "wgsl" ? device.graphicsAPI == VriGraphicsAPI_WebGPU :
                                                            (device.graphicsAPI == VriGraphicsAPI_Vulkan ||
                                                             device.graphicsAPI == VriGraphicsAPI_OpenGL ||
                                                             device.graphicsAPI == VriGraphicsAPI_OpenGLES);
        if (!compatible)
        {
            return makeError("Device backend cannot consume shader target: {}", program.profile);
        }
        if (auto valid = validateShaderProgram(program); !valid)
        {
            return valid;
        }
        if (program.rayQuery && !device.hasRayQuery)
        {
            return makeError("Shader program requires ray query");
        }
        return {};
    }

    std::expected<void, Error> detail::validateShaderProgram(const ShaderProgram& program)
    {
        const bool wgsl = program.profile == "wgsl";
        if ((!wgsl && !program.profile.starts_with("spirv_")) || program.profile.size() > 64 ||
            (wgsl && program.rayQuery))
        {
            return makeError("Unsupported shader target: {}", program.profile);
        }
        if (program.shaders.empty() || program.shaders.size() > 256)
        {
            return makeError("Shader program needs between 1 and 256 entries");
        }
        std::set<std::string> names;
        for (const auto& shader : program.shaders)
        {
            const auto& name = shader.entry.name;
            if (name.empty() || name.size() > 4096 || name.find(char(0)) != std::string::npos ||
                !names.insert(name).second || !validStage(shader.entry.stage))
            {
                return makeError("Invalid or duplicate shader entry: {}", name);
            }
            if (wgsl)
            {
                if (!shader.words.empty() || shader.wgsl.empty() || shader.wgsl.size() > kMaxFileSize ||
                    shader.wgsl.find(char(0)) != std::string::npos ||
                    (shader.entry.stage != VriShaderStage_Vertex && shader.entry.stage != VriShaderStage_Fragment &&
                     shader.entry.stage != VriShaderStage_Compute))
                {
                    return makeError("Invalid WGSL shader: {}", name);
                }
            }
            else if (!shader.wgsl.empty() || shader.words.size() < 5 || shader.words.front() != 0x07230203 ||
                     shader.words.size() > kMaxFileSize / sizeof(uint32_t))
            {
                return makeError("Invalid SPIR-V bytecode: {}", name);
            }
        }
        return {};
    }

    std::expected<std::vector<VriShaderDesc>, Error>
    ShaderProgram::descriptors(std::span<const ShaderEntry> entries) const
    {
        if (entries.empty())
        {
            return makeError("Select at least one shader entry");
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
                return makeError("Shader program has no matching entry/stage: {}", entry.name);
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

    std::expected<void, Error> ShaderProgram::save(const std::filesystem::path& file) const
    {
        if (auto valid = detail::validateShaderProgram(*this); !valid)
        {
            return valid;
        }
        auto document       = detail::encodeShaderProgram(*this);
        document["version"] = 1;
        document["kind"]    = "raw_slang";
        return detail::writeShaderArchive(file, document);
    }

    std::expected<ShaderProgram, Error> ShaderProgram::load(const std::filesystem::path& file)
    {
        const auto document = detail::readShaderArchive(file);
        if (!document)
        {
            return std::unexpected(document.error());
        }
        return programFromArchive(*document, file);
    }

    std::expected<ShaderProgram, Error> ShaderProgram::load(std::span<const std::byte>   bytes,
                                                            const std::filesystem::path& identity)
    {
        const auto document = detail::readShaderArchive(bytes, identity);
        if (!document)
        {
            return std::unexpected(document.error());
        }
        return programFromArchive(*document, identity);
    }
} // namespace vultra
