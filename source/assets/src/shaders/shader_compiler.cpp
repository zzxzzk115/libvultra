#include "rhi/shader_cache.hpp"

#include <vultra/assets/shader_asset.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/shader_compiler.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        std::string readText(const std::filesystem::path& file)
        {
            std::ifstream input(file, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error("Open shader source: " + file.string());
            }
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }

        std::string sourceBlock(const ShaderSourceBlock& block)
        {
            return "#line " + std::to_string(block.location.line) + " " +
                   nlohmann::json(block.location.file.generic_string()).dump() + "\n" + block.text +
                   "\n#line 1 \"vultra_generated_adapter\"\n";
        }

        std::string compileFingerprint(const std::filesystem::path& file,
                                       const ShaderCompileOptions&  options,
                                       std::string_view             text,
                                       std::span<const std::string> variants)
        {
            const auto sourceKey = detail::shaderCompileKey(file, text, options);
            const auto serialized =
                sourceKey + nlohmann::json(std::vector<std::string>(variants.begin(), variants.end())).dump();
            return std::to_string(XXH3_64bits(serialized.data(), serialized.size()));
        }

        ShaderState inheritState(const ShaderState& inherited, const ShaderState& override)
        {
            auto result = inherited;
            if (!override.commands.empty())
            {
                result.location = override.location;
            }
            for (const auto& [name, value] : override.commands)
            {
                result.commands.insert_or_assign(name, value);
            }
            return result;
        }

        void expandSurface(ShaderSubshader& subshader)
        {
            if (!subshader.surface)
            {
                return;
            }
            const auto&          surface = *subshader.surface;
            constexpr std::array purposes {"Forward", "DepthOnly", "ShadowCaster", "GBufferBase", "GBufferMaterial"};
            for (const auto purpose : purposes)
            {
                if (std::ranges::any_of(subshader.passes,
                                        [&](const ShaderPass& pass)
                                        {
                                            return pass.lightMode == purpose;
                                        }))
                {
                    continue;
                }
                ShaderPass pass;
                pass.name      = purpose;
                pass.lightMode = purpose;
                pass.generated = true;
                pass.location  = surface.location;
                pass.state     = surface.state;
                pass.source    = surface.source;
                pass.entries   = {{"vultraVertexMain", VriShaderStage_Vertex},
                                  {"vultraFragmentMain", VriShaderStage_Fragment}};
                subshader.passes.push_back(std::move(pass));
            }
            ShaderPass mesh;
            mesh.name             = "MeshForward";
            mesh.lightMode        = "Custom";
            mesh.generated        = true;
            mesh.requiredFeatures = VriFeature_MeshShader;
            mesh.location         = surface.location;
            mesh.state            = surface.state;
            mesh.source           = surface.source;
            mesh.entries          = {{"vultraTaskMain", VriShaderStage_Task},
                                     {"vultraMeshMain", VriShaderStage_Mesh},
                                     {"vultraFragmentMain", VriShaderStage_Fragment}};
            subshader.passes.push_back(std::move(mesh));
        }
    } // namespace

    std::string ShaderAsset::generatedMaterialSource() const
    {
        std::string result = "#line 1 \"vultra_generated_material\"\nstruct VultraMaterialProperties\n{\n";
        if (properties.empty())
        {
            result += "    uint __vultraEmpty;\n";
        }
        constexpr std::array textureTypes {"Texture2D<float4>",
                                           "Texture2DArray<float4>",
                                           "Texture3D<float4>",
                                           "TextureCube<float4>",
                                           "TextureCubeArray<float4>"};
        for (const auto& property : properties)
        {
            result += "#line " + std::to_string(property.location.line) + " " +
                      nlohmann::json(property.location.file.generic_string()).dump() + "\n";
            std::string type = "float";
            if (property.type == ShaderPropertyType::eInteger)
            {
                type = "int";
            }
            else if (property.type == ShaderPropertyType::eBoolean)
            {
                type = "bool";
            }
            else if (property.type == ShaderPropertyType::eVector || property.type == ShaderPropertyType::eColor)
            {
                type = "float4";
            }
            else if (property.isTexture())
            {
                type = textureTypes[size_t(property.type) - size_t(ShaderPropertyType::eTexture2D)];
            }
            result += "    " + type + " " + property.name + ";\n";
            if (property.isTexture())
            {
                result += "    SamplerState " + property.name + "Sampler;\n";
                if (!property.noScaleOffset && (property.type == ShaderPropertyType::eTexture2D ||
                                                property.type == ShaderPropertyType::eTexture2DArray))
                {
                    result += "    float4 " + property.name + "ScaleOffset;\n";
                }
                if (property.normal)
                {
                    result += "    uint " + property.name + "Encoding;\n";
                }
            }
        }
        result += "#line 1 \"vultra_generated_material\"\n};\nParameterBlock<VultraMaterialProperties> material;\n";
        return result;
    }

    std::string ShaderAsset::generatedPassSource(uint32_t subshaderIndex, uint32_t passIndex) const
    {
        const auto& subshader = subshaders.at(subshaderIndex);
        const auto& pass      = subshader.passes.at(passIndex);
        std::string source    = generatedMaterialSource();
        if (pass.generated)
        {
            if (!subshader.surface)
            {
                throw std::invalid_argument("Generated Pass has no Surface");
            }
            source += "#include \"resources/game_surface.slangh\"\n";
        }
        for (const auto& block : subshader.common)
        {
            source += sourceBlock(block);
        }
        source += sourceBlock(pass.source);
        if (pass.generated)
        {
            source += "#line 1 \"vultra_generated_surface\"\n";
            if (subshader.tags.contains("RenderType") && subshader.tags.at("RenderType") == "AlphaTest")
            {
                source += "#define VULTRA_SURFACE_ALPHA_MASK 1\n";
            }
            source += "SurfaceOutput vultraEvaluateSurface(SurfaceInput input)\n{\n";
            source += "    SurfaceOutput output = defaultSurfaceOutput();\n";
            source += "    " + subshader.surface->entry + "(input, output);\n    return output;\n}\n";
            if (pass.name == "MeshForward")
            {
                source += "#define VULTRA_SURFACE_MESH 1\n";
            }
            if (pass.lightMode == "ShadowCaster")
            {
                source += "#define VULTRA_SURFACE_SHADOW 1\n";
            }
            else if (pass.lightMode == "DepthOnly")
            {
                source += "#define VULTRA_SURFACE_DEPTH 1\n";
            }
            else if (pass.lightMode == "GBufferBase")
            {
                source += "#define VULTRA_SURFACE_GBUFFER_BASE 1\n";
            }
            else if (pass.lightMode == "GBufferMaterial")
            {
                source += "#define VULTRA_SURFACE_GBUFFER_MATERIAL 1\n";
            }
            source += "#include \"passes/game_surface.slangh\"\n";
        }
        return source;
    }

    ShaderAsset ShaderAsset::compile(const std::filesystem::path& file,
                                     const ShaderCompileOptions&  options,
                                     std::span<const std::string> selectedVariants)
    {
        const auto text  = readText(file);
        auto       asset = compileSource(file, text, options, selectedVariants);
        if (readText(file) != text)
        {
            throw std::runtime_error("Shader source changed while compiling: " + file.string());
        }
        return asset;
    }

    ShaderAsset ShaderAsset::compileSource(const std::filesystem::path& file,
                                           std::string_view             text,
                                           const ShaderCompileOptions&  options,
                                           std::span<const std::string> selectedVariants)
    {
        auto asset       = parse(file, text);
        asset.compileKey = compileFingerprint(file, options, text, selectedVariants);
        if (!options.entries.empty())
        {
            throw std::invalid_argument("Game shader entries are declared in its Passes, not compile options");
        }
        if (!selectedVariants.empty())
        {
            std::set<std::string> selected;
            for (const auto& name : selectedVariants)
            {
                asset.variant(name);
                if (!selected.insert(name).second)
                {
                    throw std::invalid_argument("Duplicate selected shader variant");
                }
            }
            std::erase_if(asset.variants,
                          [&](const ShaderVariant& variant)
                          {
                              return !selected.contains(variant.name);
                          });
        }
        for (auto& subshader : asset.subshaders)
        {
            expandSurface(subshader);
        }
        ShaderCompiler compiler;
        for (uint32_t subshaderIndex = 0; subshaderIndex < asset.subshaders.size(); ++subshaderIndex)
        {
            auto& subshader = asset.subshaders[subshaderIndex];
            for (uint32_t passIndex = 0; passIndex < subshader.passes.size(); ++passIndex)
            {
                auto& pass = subshader.passes[passIndex];
                if (pass.entries.front().stage != VriShaderStage_Compute)
                {
                    pass.state = inheritState(subshader.state, pass.state);
                }
                const auto source = asset.generatedPassSource(subshaderIndex, passIndex);
                for (const auto& variant : asset.variants)
                {
                    auto settings     = options;
                    settings.entries  = pass.entries;
                    settings.rayQuery = settings.rayQuery ||
                                        ((pass.requiredFeatures | subshader.requiredFeatures) & VriFeature_RayQuery);
                    settings.defines.insert(settings.defines.end(), variant.defines.begin(), variant.defines.end());
                    settings.linkModules.insert(settings.linkModules.end(),
                                                variant.modules.begin(),
                                                variant.modules.end());
                    if (!variant.constants.empty())
                    {
                        settings.linkSources.push_back({"vultra_variant_constants", variant.constants});
                    }
                    ShaderProgram program;
                    try
                    {
                        program = compiler.compileSource(asset.sourcePath, source, settings);
                    }
                    catch (const std::exception& error)
                    {
                        throw std::runtime_error(pass.location.describe() + ": Pass " + pass.name + ", variant " +
                                                 variant.name + "\n" + error.what());
                    }
                    asset.diagnostics += program.diagnostics;
                    pass.programs.emplace(variant.name, std::move(program));
                }
            }
        }
        for (const auto& subshader : asset.subshaders)
        {
            for (const auto& pass : subshader.passes)
            {
                for (const auto& [name, program] : pass.programs)
                {
                    if (!detail::shaderDependenciesCurrent(program.dependencies, file, options))
                    {
                        throw std::runtime_error("Shader asset dependency changed while compiling: " + file.string());
                    }
                }
            }
        }
        asset.validate();
        return asset;
    }

    std::vector<ShaderSourceProjection> ShaderAsset::projectSources() const
    {
        auto expanded = *this;
        for (auto& subshader : expanded.subshaders)
        {
            expandSurface(subshader);
        }
        std::vector<ShaderSourceProjection> documents;
        for (uint32_t sub = 0; sub < expanded.subshaders.size(); ++sub)
        {
            const auto& subshader = expanded.subshaders[sub];
            for (uint32_t pass = 0; pass < subshader.passes.size(); ++pass)
            {
                ShaderSourceProjection document;
                document.name = "subshader_" + std::to_string(sub) + "_pass_" + std::to_string(pass);
                document.text = expanded.generatedPassSource(sub, pass);
                for (const auto& property : properties)
                {
                    const auto position = document.text.find(" " + property.name + ";\n");
                    if (position == std::string::npos)
                    {
                        throw std::logic_error("Generated property declaration was lost");
                    }
                    const auto line =
                        uint32_t(std::count(document.text.begin(), document.text.begin() + position, '\n')) + 1;
                    const auto column = uint32_t(position - document.text.rfind('\n', position));
                    document.mappings.push_back({line, 1, property.location, false, column + 1});
                }
                size_t cursor = 0;
                for (const auto& block : subshader.common)
                {
                    document.mapBlock(block, cursor);
                }
                document.mapBlock(subshader.passes[pass].source, cursor);
                // Slangd reports #line-adjusted diagnostic lines but physical completion/definition positions.
                // Remove adapter directives only; native program text and line counts stay intact.
                std::istringstream input(document.text);
                std::string        projected;
                std::string        line;
                uint32_t           number = 1;
                while (std::getline(input, line))
                {
                    const bool native = std::ranges::any_of(document.mappings,
                                                            [&](const ShaderSourceMapping& range)
                                                            {
                                                                return range.editable &&
                                                                       number >= range.generatedLine &&
                                                                       number < range.generatedLine + range.lineCount;
                                                            });
                    if (native || !line.starts_with("#line "))
                    {
                        projected += line;
                    }
                    projected += '\n';
                    ++number;
                }
                document.text = std::move(projected);
                documents.push_back(std::move(document));
            }
        }
        return documents;
    }

    bool ShaderAsset::cook(const std::filesystem::path& source,
                           const std::filesystem::path& output,
                           const ShaderCompileOptions&  options,
                           std::span<const std::string> variants)
    {
        const auto key = compileFingerprint(source, options, readText(source), variants);
        if (std::filesystem::is_regular_file(output))
        {
            try
            {
                const auto previous = load(output);
                bool       current  = previous.compileKey == key;
                for (const auto& subshader : previous.subshaders)
                {
                    for (const auto& pass : subshader.passes)
                    {
                        for (const auto& [variant, program] : pass.programs)
                        {
                            current =
                                detail::shaderDependenciesCurrent(program.dependencies, source, options) && current;
                        }
                    }
                }
                if (current)
                {
                    Logger::core().info("Shader cache hit: {}", source.string());
                    return false;
                }
            }
            catch (const std::exception& error)
            {
                Logger::core().warn("Rebuilding unreadable shader cache {}: {}", output.string(), error.what());
            }
            Logger::core().info("Shader source, options or dependency changed: {}", source.string());
        }
        Logger::core().info("Cooking game shader: {}", source.string());
        auto asset = compile(source, options, variants);
        asset.save(output);
        if (!asset.diagnostics.empty())
        {
            Logger::core().warn("{}", asset.diagnostics);
        }
        Logger::core().info("Cooked shader {}: {} SubShaders, {} explicit variants",
                            asset.name,
                            asset.subshaders.size(),
                            asset.variants.size());
        return true;
    }
} // namespace vultra
