#include <vultra/assets/shader_asset.hpp>
#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

namespace
{
    nlohmann::json describe(const vultra::ShaderParameter& parameter)
    {
        nlohmann::json node {{"name", parameter.name},
                             {"kind", parameter.kind},
                             {"size", parameter.size},
                             {"stride", parameter.stride},
                             {"count", parameter.count},
                             {"rows", parameter.rows},
                             {"columns", parameter.columns},
                             {"column_major", parameter.columnMajor},
                             {"matrix_stride", parameter.matrixStride},
                             {"shape", parameter.resourceShape},
                             {"access", parameter.resourceAccess},
                             {"offsets", nlohmann::json::array()},
                             {"fields", nlohmann::json::array()}};
        for (const auto& offset : parameter.offsets)
        {
            node["offsets"].push_back({{"kind", offset.kind}, {"offset", offset.offset}, {"space", offset.space}});
        }
        for (const auto& field : parameter.fields)
        {
            node["fields"].push_back(describe(field));
        }
        return node;
    }

    void writeReflection(const std::string& output, const nlohmann::json& reflection)
    {
        const auto text = reflection.dump(2);
        vultra::writeFileAtomically(output, std::as_bytes(std::span(text.data(), text.size())));
    }
} // namespace

int main(int argc, char** argv)
try
{
    argparse::ArgumentParser cli("vultra-shader", "0.1.0", argparse::default_arguments::none);
    cli.add_description("Cook game .vshader assets or raw .slang programs to checked .vshaderc artifacts");
    vultra::addAppOptions(cli);
    cli.add_argument("source").help("Source .vshader asset or .slang module");
    cli.add_argument("--output").help("Output .vshaderc file, atomically replaced after successful cooking");
    cli.add_argument("--include").append().help("Additional include directory; may be repeated");
    cli.add_argument("--variant").append().help("Selected game variant; repeated, defaults to all declared variants");
    cli.add_argument("--entry").append().help(
        "Raw stage:name entry; raster, compute, task/mesh and ray-tracing stages are supported");
    cli.add_argument("--define").append().help("Preprocessor macro NAME=VALUE; may be repeated");
    cli.add_argument("--capability").append().help("Explicit Slang target capability; may be repeated");
    cli.add_argument("--link").append().help("Additional Slang link module; may be repeated");
    cli.add_argument("--profile").default_value(std::string("spirv_1_5")).help("Slang SPIR-V target profile");
    cli.add_argument("--ray-query").flag().help("Enable the SPIR-V ray-query capability");
    cli.add_argument("--project-source").help("Write generated Slang documents and source maps for editor services");
    cli.add_argument("--input-text").help("Read unsaved source text from this file using the original source path");
    cli.add_argument("--check").flag().help(
        "Compile unsaved game source for diagnostics without replacing an artifact");
    cli.add_argument("--reflection").help("Write target layout JSON for inspection");
    if (!vultra::parseCommandLine(cli, argc, argv))
    {
        return 0;
    }
    vultra::ShaderCompileOptions options;
    options.profile  = cli.get<std::string>("--profile");
    options.rayQuery = cli.get<bool>("--ray-query");
    if (auto paths = cli.present<std::vector<std::string>>("--include"))
    {
        for (const auto& path : *paths)
        {
            options.includeDirectories.emplace_back(path);
        }
    }
    if (auto paths = cli.present<std::vector<std::string>>("--link"))
    {
        for (const auto& path : *paths)
        {
            options.linkModules.emplace_back(path);
        }
    }
    if (auto values = cli.present<std::vector<std::string>>("--entry"))
    {
        const std::map<std::string, VriShaderStageBits> stages {{"vertex", VriShaderStage_Vertex},
                                                                {"fragment", VriShaderStage_Fragment},
                                                                {"compute", VriShaderStage_Compute},
                                                                {"task", VriShaderStage_Task},
                                                                {"mesh", VriShaderStage_Mesh},
                                                                {"geometry", VriShaderStage_Geometry},
                                                                {"hull", VriShaderStage_TessControl},
                                                                {"domain", VriShaderStage_TessEval},
                                                                {"raygen", VriShaderStage_RayGen},
                                                                {"intersection", VriShaderStage_Intersection},
                                                                {"anyhit", VriShaderStage_AnyHit},
                                                                {"closesthit", VriShaderStage_ClosestHit},
                                                                {"miss", VriShaderStage_Miss},
                                                                {"callable", VriShaderStage_Callable}};
        for (const auto& value : *values)
        {
            const auto separator = value.find(':');
            const auto stage     = stages.find(value.substr(0, separator));
            if (separator == std::string::npos || stage == stages.end() || separator + 1 == value.size())
            {
                throw std::invalid_argument("Entry requires a supported stage:name: " + value);
            }
            options.entries.push_back({value.substr(separator + 1), stage->second});
        }
    }
    if (auto values = cli.present<std::vector<std::string>>("--define"))
    {
        for (const auto& value : *values)
        {
            const auto separator = value.find('=');
            if (separator == std::string::npos || separator == 0)
            {
                throw std::invalid_argument("Macro requires NAME=VALUE: " + value);
            }
            options.defines.push_back({value.substr(0, separator), value.substr(separator + 1)});
        }
    }
    options.capabilities = cli.present<std::vector<std::string>>("--capability").value_or(std::vector<std::string> {});
    const auto source    = std::filesystem::path(cli.get<std::string>("source"));
    if (const auto directory = cli.present<std::string>("--project-source"))
    {
        if (source.extension() != ".vshader")
        {
            throw std::invalid_argument("Projection requires a game .vshader source");
        }
        const auto    input = cli.present<std::string>("--input-text").value_or(source.string());
        std::ifstream stream(input, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Open shader editor text: " + input);
        }
        const std::string text {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        const auto        asset = vultra::ShaderAsset::parse(source, text);
        nlohmann::json    maps  = nlohmann::json::array();
        const auto        root  = std::filesystem::absolute(*directory);
        std::filesystem::create_directories(root);
        for (const auto& document : asset.projectSources())
        {
            const auto file = root / (document.name + ".slang");
            vultra::writeFileAtomically(file, std::as_bytes(std::span(document.text.data(), document.text.size())));
            nlohmann::json map = nlohmann::json::array();
            for (const auto& range : document.mappings)
            {
                map.push_back({{"generated_line", range.generatedLine},
                               {"line_count", range.lineCount},
                               {"editable", range.editable},
                               {"generated_column", range.generatedColumn},
                               {"file", range.original.file.generic_string()},
                               {"line", range.original.line},
                               {"column", range.original.column}});
            }
            maps.push_back({{"file", file.generic_string()}, {"mappings", map}});
        }
        writeReflection((root / "source_map.json").string(), maps);
        if (cli.get<bool>("--check"))
        {
            vultra::ShaderAsset::compileSource(source, text, options);
        }
        return 0;
    }
    if (cli.get<bool>("--check") || cli.present<std::string>("--input-text"))
    {
        throw std::invalid_argument("--check and --input-text require --project-source");
    }
    const auto output = std::filesystem::path(cli.get<std::string>("--output"));
    if ((source.extension() != ".slang" && source.extension() != ".vshader") || output.extension() != ".vshaderc")
    {
        throw std::invalid_argument("Input must be .slang or .vshader and output must be .vshaderc");
    }
    if (source.extension() == ".vshader")
    {
        const auto variants = cli.present<std::vector<std::string>>("--variant").value_or(std::vector<std::string> {});
        vultra::ShaderAsset::cook(source, output, options, variants);
        if (const auto reflection = cli.present<std::string>("--reflection"))
        {
            const auto     asset   = vultra::ShaderAsset::load(output);
            nlohmann::json layouts = nlohmann::json::array();
            for (const auto& subshader : asset.subshaders)
            {
                for (const auto& pass : subshader.passes)
                {
                    for (const auto& [variant, program] : pass.programs)
                    {
                        layouts.push_back(
                            {{"pass", pass.name}, {"variant", variant}, {"parameters", describe(program.parameters)}});
                    }
                }
            }
            writeReflection(*reflection, layouts);
        }
        return 0;
    }
    if (cli.present<std::vector<std::string>>("--variant"))
    {
        throw std::invalid_argument("Raw Slang specialization uses explicit link modules and compile options");
    }
    vultra::Logger::app().info("Cooking raw Slang shader {}", source.string());
    vultra::ShaderProgram::cook(source, output, options);
    const auto program = vultra::ShaderProgram::load(output);
    if (!program.diagnostics.empty())
    {
        vultra::Logger::app().warn("{}", program.diagnostics);
    }

    if (const auto reflection = cli.present<std::string>("--reflection"))
    {
        writeReflection(*reflection, describe(program.parameters));
    }
    vultra::Logger::app().info("Cooked {} entry points into {}", program.shaders.size(), output.string());
    return 0;
}
catch (const std::exception& error)
{
    vultra::Logger::app().error("{}", error.what());
    return 1;
}
