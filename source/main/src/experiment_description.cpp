#include <vultra/main/experiment_description.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        std::filesystem::path resolvePath(const Json& value, const std::filesystem::path& directory)
        {
            const auto text = value.get<std::string>();
            if (text.empty() || text.contains('\0'))
            {
                throw std::invalid_argument("Experiment path must be nonempty UTF-8 text without NUL bytes");
            }
            const auto path = std::filesystem::path(std::u8string(text.begin(), text.end()));
            return std::filesystem::absolute(directory / path).lexically_normal();
        }

        std::string portablePath(const std::filesystem::path& path, const std::filesystem::path& directory)
        {
            const auto relative = std::filesystem::absolute(path).lexically_relative(directory);
            const auto text     = (relative.empty() ? path : relative).generic_u8string();
            return {text.begin(), text.end()};
        }

        glm::mat4 matrix(const Json& data)
        {
            if (!data.is_array() || data.size() != 16)
            {
                throw std::invalid_argument("Experiment camera matrices require 16 column-major values");
            }
            glm::mat4 value;
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    value[column][row] = data.at(column * 4 + row).get<float>();
                }
            }
            return value;
        }

        Json matrix(const glm::mat4& value)
        {
            auto data = Json::array();
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    data.push_back(value[column][row]);
                }
            }
            return data;
        }

        bool validMatrix(const glm::mat4& value)
        {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(value[column][row]))
                    {
                        return false;
                    }
                }
            }
            const auto determinant = glm::determinant(value);
            return std::isfinite(determinant) && determinant != 0;
        }
    } // namespace

    void ExperimentDescription::validate() const
    {
        if (config.input.empty())
        {
            throw std::invalid_argument("Experiment input is required");
        }
        if (config.size.empty() || frames == 0)
        {
            throw std::invalid_argument("Experiment requires nonzero dimensions/frames");
        }
        if (warmup > UINT64_MAX - frames)
        {
            throw std::invalid_argument("Experiment warmup plus measured frames overflows uint64");
        }
        if (!std::isfinite(timeStep) || timeStep <= 0)
        {
            throw std::invalid_argument("Experiment time_step must be finite and positive");
        }
        if (config.path != RenderPath::eNaiveDeferred && config.path != RenderPath::eNaiveForward &&
            config.path != RenderPath::eReferencePathTracing)
        {
            throw std::invalid_argument("Unknown experiment render path");
        }
        if (config.camera)
        {
            const auto& camera = *config.camera;
            if (!validMatrix(camera.view) || !validMatrix(camera.projection) || !std::isfinite(camera.nearPlane) ||
                !std::isfinite(camera.farPlane) || camera.nearPlane <= 0 || camera.farPlane <= camera.nearPlane)
            {
                throw std::invalid_argument(
                    "Experiment camera requires finite invertible matrices and a positive depth range");
            }
        }
    }

    ExperimentDescription ExperimentDescription::load(const std::filesystem::path& file)
    {
        try
        {
            std::ifstream stream(file, std::ios::binary);
            if (!stream)
            {
                throw std::runtime_error("Cannot open experiment description");
            }
            const auto data = Json::parse(stream);
            if (data.at("format") != "vultra.experiment" || data.at("version") != 1)
            {
                throw std::invalid_argument("Expected vultra.experiment version 1");
            }
            ExperimentDescription result;
            const auto            directory = std::filesystem::absolute(file).parent_path();
            result.config.input             = resolvePath(data.at("input"), directory);
            for (const auto* field : {"width", "height"})
            {
                if (!data.at(field).is_number_unsigned() || data.at(field).get<uint64_t>() > UINT32_MAX)
                {
                    throw std::invalid_argument(std::string("Experiment ") + field + " requires a uint32 dimension");
                }
            }
            result.config.size = {data.at("width").get<uint32_t>(), data.at("height").get<uint32_t>()};
            const auto path    = data.at("path").get<std::string>();
            if (path == "reference")
            {
                result.config.path = RenderPath::eReferencePathTracing;
            }
            else if (path == "forward")
            {
                result.config.path = RenderPath::eNaiveForward;
            }
            else if (path != "deferred")
            {
                throw std::invalid_argument("Unknown experiment render path: " + path);
            }
            if (!data.at("seed").is_number_unsigned() || data.at("seed").get<uint64_t>() > UINT32_MAX ||
                !data.at("frames").is_number_unsigned() || !data.at("warmup").is_number_unsigned())
            {
                throw std::invalid_argument(
                    "Experiment seed/frames/warmup require unsigned integers within their ranges");
            }
            result.config.seed = data.at("seed").get<uint32_t>();
            result.frames      = data.at("frames").get<uint64_t>();
            result.warmup      = data.at("warmup").get<uint64_t>();
            result.timeStep    = data.at("time_step").get<float>();
            if (!data.at("environment").is_null())
            {
                result.config.environment = resolvePath(data.at("environment"), directory);
            }
            if (!data.at("graph").is_null())
            {
                result.graph = GraphDefinition::parse(data.at("graph").dump());
            }
            if (!data.at("camera").is_null())
            {
                const auto& camera   = data.at("camera");
                result.config.camera = RenderCamera {matrix(camera.at("view")),
                                                     matrix(camera.at("projection")),
                                                     camera.at("near").get<float>(),
                                                     camera.at("far").get<float>()};
            }
            result.validate();
            return result;
        }
        catch (const std::exception& error)
        {
            throw std::invalid_argument(file.string() + ": " + error.what());
        }
    }

    void ExperimentDescription::save(const std::filesystem::path& file) const
    {
        validate();
        const auto  directory = std::filesystem::absolute(file).parent_path();
        std::string path      = "deferred";
        if (config.path == RenderPath::eNaiveForward)
        {
            path = "forward";
        }
        else if (config.path == RenderPath::eReferencePathTracing)
        {
            path = "reference";
        }
        Json data {{"format", "vultra.experiment"},
                   {"version", 1},
                   {"input", portablePath(config.input, directory)},
                   {"width", config.size.width},
                   {"height", config.size.height},
                   {"path", path},
                   {"seed", config.seed},
                   {"frames", frames},
                   {"warmup", warmup},
                   {"time_step", timeStep},
                   {"environment", nullptr},
                   {"graph", nullptr},
                   {"camera", nullptr}};
        if (!config.environment.empty())
        {
            data["environment"] = portablePath(config.environment, directory);
        }
        if (graph)
        {
            data["graph"] = Json::parse(graph->serialize());
        }
        if (config.camera)
        {
            const auto& camera = *config.camera;
            data["camera"]     = {{"view", matrix(camera.view)},
                                  {"projection", matrix(camera.projection)},
                                  {"near", camera.nearPlane},
                                  {"far", camera.farPlane}};
        }
        const auto text = data.dump(2) + '\n';
        writeFileAtomically(file, std::as_bytes(std::span(text)));
    }
} // namespace vultra
