#include "shader_archive.hpp"

#include <vultra/gpu/device.hpp>
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

        // An archive holds one program per target, all compiled from the same source.
        std::expected<std::vector<ShaderProgram>, Error> programsFromArchive(const nlohmann::json&        document,
                                                                             const std::filesystem::path& identity)
        {
            // readShaderArchive has checked that the kind is present.
            if (document.at("kind") != "raw_slang")
            {
                return makeError("ShaderProgram requires a raw Slang artifact: {}", identity.string());
            }
            const auto programs = document.find("programs");
            if (programs == document.end() || !programs->is_array() || programs->empty() || programs->size() > 16)
            {
                return makeError("Invalid cooked shader targets: {}", identity.string());
            }
            std::vector<ShaderProgram> result;
            for (const auto& entry : *programs)
            {
                auto program = detail::decodeShaderProgram(entry);
                if (!program)
                {
                    return std::unexpected(program.error());
                }
                if (auto valid = detail::validateShaderProgram(*program); !valid)
                {
                    return std::unexpected(valid.error());
                }
                if (std::ranges::find(result, program->profile, &ShaderProgram::profile) != result.end())
                {
                    return makeError("Cooked shader repeats target {}: {}", program->profile, identity.string());
                }
                result.push_back(std::move(*program));
            }
            return result;
        }

        std::string targetNames(std::span<const ShaderProgram> programs)
        {
            std::string names;
            for (const auto& program : programs)
            {
                names += (names.empty() ? "" : ", ") + program.profile;
            }
            return names;
        }

        std::expected<ShaderProgram, Error> selectTarget(std::expected<std::vector<ShaderProgram>, Error> programs,
                                                         std::string_view                                 profile,
                                                         const std::filesystem::path&                     identity)
        {
            if (!programs)
            {
                return std::unexpected(programs.error());
            }
            const auto found = std::ranges::find(*programs, profile, &ShaderProgram::profile);
            if (found == programs->end())
            {
                return makeError("{} has no shaders for {}; it was cooked for {}",
                                 identity.string(),
                                 profile,
                                 targetNames(*programs));
            }
            return std::move(*found);
        }

        std::expected<ShaderProgram, Error> onlyTarget(std::expected<std::vector<ShaderProgram>, Error> programs,
                                                       const std::filesystem::path&                     identity)
        {
            if (!programs)
            {
                return std::unexpected(programs.error());
            }
            if (programs->size() != 1)
            {
                return makeError("{} holds shaders for {}; load the one a device needs",
                                 identity.string(),
                                 targetNames(*programs));
            }
            return std::move(programs->front());
        }
    } // namespace

    std::string shaderProfile(VriGraphicsAPI api)
    {
        if (api == VriGraphicsAPI_WebGPU)
        {
            return "wgsl";
        }
        // Shader model 6.6 covers mesh shading, ray queries, wave operations and 64-bit atomics.
        return api == VriGraphicsAPI_D3D12 ? "sm_6_6" : "spirv_1_5";
    }

    std::vector<std::string> buildShaderProfiles()
    {
        std::vector<std::string> profiles;
        for (const auto api : Device::graphicsAPIs())
        {
            auto profile = shaderProfile(api);
            if (std::ranges::find(profiles, profile) == profiles.end())
            {
                profiles.push_back(std::move(profile));
            }
        }
        return profiles;
    }

    std::expected<void, Error> detail::validateShaderProgram(const ShaderProgram& program, const VriDeviceDesc& device)
    {
        // VRI's Metal and GL backends consume SPIR-V through their own SPIRV-Cross path.
        bool compatible = device.graphicsAPI == VriGraphicsAPI_Vulkan || device.graphicsAPI == VriGraphicsAPI_Metal ||
                          device.graphicsAPI == VriGraphicsAPI_OpenGL || device.graphicsAPI == VriGraphicsAPI_OpenGLES;
        if (program.profile == "wgsl")
        {
            compatible = device.graphicsAPI == VriGraphicsAPI_WebGPU;
        }
        else if (isD3DProfile(program.profile))
        {
            compatible = device.graphicsAPI == VriGraphicsAPI_D3D12;
        }
        if (!compatible)
        {
            return makeError("Shaders compiled for {} cannot run on this device, which needs {}: compile or cook them "
                             "with a build of the device's graphics API",
                             program.profile,
                             shaderProfile(device.graphicsAPI));
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
        if ((!wgsl && !program.profile.starts_with("spirv_") && !isD3DProfile(program.profile)) ||
            program.profile.size() > 64 || (wgsl && program.rayQuery))
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
            else if (!shader.wgsl.empty() || shader.words.size() < 5 ||
                     shader.words.front() != bytecodeMagic(program.profile) ||
                     shader.words.size() > kMaxFileSize / sizeof(uint32_t))
            {
                return makeError("Invalid {} bytecode: {}", program.profile, name);
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

    bool ShaderProgram::hasDebugInfo() const
    {
        if (profile == "wgsl")
        {
            return false;
        }
        // SPIR-V names its debug instruction set; a DXIL container lists its debug DXIL as an "ILDB" part.
        const std::string_view marker = isD3DProfile(profile) ? "ILDB" : "NonSemantic.Shader.DebugInfo";
        return std::ranges::any_of(shaders,
                                   [marker](const CompiledShader& shader)
                                   {
                                       const std::string_view bytes(reinterpret_cast<const char*>(shader.words.data()),
                                                                    shader.words.size() * sizeof(uint32_t));
                                       return bytes.find(marker) != std::string_view::npos;
                                   });
    }

    std::expected<void, Error> ShaderProgram::save(const std::filesystem::path& file) const
    {
        return save(file, std::span(this, 1));
    }

    std::expected<void, Error> ShaderProgram::save(const std::filesystem::path&   file,
                                                   std::span<const ShaderProgram> targets)
    {
        if (targets.empty() || targets.size() > 16)
        {
            return makeError("A cooked shader holds 1 to 16 targets: {}", file.string());
        }
        nlohmann::json programs = nlohmann::json::array();
        for (const auto& program : targets)
        {
            if (auto valid = detail::validateShaderProgram(program); !valid)
            {
                return valid;
            }
            if (std::ranges::count(targets, program.profile, &ShaderProgram::profile) != 1)
            {
                return makeError("A cooked shader holds one program per target, {} twice: {}",
                                 program.profile,
                                 file.string());
            }
            programs.push_back(detail::encodeShaderProgram(program));
        }
        return detail::writeShaderArchive(file, {{"version", 1}, {"kind", "raw_slang"}, {"programs", programs}});
    }

    std::expected<std::vector<ShaderProgram>, Error> ShaderProgram::loadTargets(const std::filesystem::path& file)
    {
        const auto document = detail::readShaderArchive(file);
        if (!document)
        {
            return std::unexpected(document.error());
        }
        return programsFromArchive(*document, file);
    }

    std::expected<std::vector<ShaderProgram>, Error> ShaderProgram::loadTargets(std::span<const std::byte>   bytes,
                                                                                const std::filesystem::path& identity)
    {
        const auto document = detail::readShaderArchive(bytes, identity);
        if (!document)
        {
            return std::unexpected(document.error());
        }
        return programsFromArchive(*document, identity);
    }

    std::expected<ShaderProgram, Error> ShaderProgram::load(const std::filesystem::path& file)
    {
        return onlyTarget(loadTargets(file), file);
    }

    std::expected<ShaderProgram, Error> ShaderProgram::load(std::span<const std::byte>   bytes,
                                                            const std::filesystem::path& identity)
    {
        return onlyTarget(loadTargets(bytes, identity), identity);
    }

    std::expected<ShaderProgram, Error> ShaderProgram::load(const std::filesystem::path& file, std::string_view profile)
    {
        return selectTarget(loadTargets(file), profile, file);
    }

    std::expected<ShaderProgram, Error> ShaderProgram::load(std::span<const std::byte>   bytes,
                                                            const std::filesystem::path& identity,
                                                            std::string_view             profile)
    {
        return selectTarget(loadTargets(bytes, identity), profile, identity);
    }
} // namespace vultra
