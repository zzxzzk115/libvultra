#include <vultra/core/base/command_line.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/shader/shader_program.hpp>

#include <nlohmann/json.hpp>

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
                             {"descriptor_type",
                              parameter.descriptorType ? nlohmann::json(*parameter.descriptorType) : nlohmann::json()},
                             {"view_type", parameter.viewType},
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

    std::expected<void, vultra::Error> writeReflection(const std::string& output, const nlohmann::json& reflection)
    {
        std::string text;
        // dump() reports names that are not UTF-8 only by throwing.
        try
        {
            text = reflection.dump(2);
        }
        catch (const nlohmann::json::type_error& error)
        {
            return vultra::makeError("Write reflection {}: {}", output, error.what());
        }
        return vultra::writeFileAtomically(output, std::as_bytes(std::span(text.data(), text.size())));
    }

    std::expected<void, vultra::Error> run(int argc, char** argv)
    {
        argparse::ArgumentParser cli("vultra-shader", "0.1.0", argparse::default_arguments::none);
        cli.add_description("Cook a .slang program to a checked .vshaderc artifact");
        vultra::addAppOptions(cli);
        cli.add_argument("source").help("Source .slang module");
        cli.add_argument("--output").help("Output .vshaderc file, atomically replaced after successful cooking");
        cli.add_argument("--include").append().help("Additional include directory; may be repeated");
        cli.add_argument("--entry").append().help(
            "stage:name entry; raster, compute, task/mesh and ray-tracing stages are supported");
        cli.add_argument("--define").append().help("Preprocessor macro NAME=VALUE; may be repeated");
        cli.add_argument("--capability").append().help("Explicit Slang target capability; may be repeated");
        cli.add_argument("--link").append().help("Additional Slang link module; may be repeated");
        cli.add_argument("--profile").default_value(std::string("spirv_1_5")).help("SPIR-V target profile or wgsl");
        cli.add_argument("--ray-query").flag().help("Enable the SPIR-V ray-query capability");
        cli.add_argument("--reflection").help("Write target layout JSON for inspection");
        const auto parsed = vultra::parseCommandLine(cli, argc, argv);
        if (!parsed)
        {
            return std::unexpected(parsed.error());
        }
        if (!*parsed)
        {
            return {};
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
                    return vultra::makeError("Entry requires a supported stage:name: {}", value);
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
                    return vultra::makeError("Macro requires NAME=VALUE: {}", value);
                }
                options.defines.push_back({value.substr(0, separator), value.substr(separator + 1)});
            }
        }
        options.capabilities =
            cli.present<std::vector<std::string>>("--capability").value_or(std::vector<std::string> {});
        const auto source = std::filesystem::path(cli.get<std::string>("source"));
        // argparse's get() throws for an option given no value; present() reports it as absent.
        const auto output = std::filesystem::path(cli.present<std::string>("--output").value_or(""));
        if (source.extension() != ".slang" || output.extension() != ".vshaderc")
        {
            return vultra::makeError("Input must be .slang and output must be .vshaderc");
        }
        vultra::Logger::app().info("Cooking Slang shader {}", source.string());
        if (auto cooked = vultra::ShaderProgram::cook(source, output, options); !cooked)
        {
            return std::unexpected(cooked.error());
        }
        const auto program = vultra::ShaderProgram::load(output);
        if (!program)
        {
            return std::unexpected(program.error());
        }
        if (!program->diagnostics.empty())
        {
            vultra::Logger::app().warn("{}", program->diagnostics);
        }

        if (const auto reflection = cli.present<std::string>("--reflection"))
        {
            if (auto written = writeReflection(*reflection, describe(program->parameters)); !written)
            {
                return written;
            }
        }
        vultra::Logger::app().info("Cooked {} entry points into {}", program->shaders.size(), output.string());
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (auto result = run(argc, argv); !result)
    {
        vultra::Logger::app().error("{}", result.error().message);
        return 1;
    }
    return 0;
}
