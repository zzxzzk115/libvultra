#include <vultra/core/base/stable_id.hpp>
#include <vultra/main/experiment_description.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
    const auto directory =
        std::filesystem::absolute("build/.tmp/experiment-description-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    const auto            file = directory / "baseline.vexperiment";
    ExperimentDescription baseline;
    baseline.config.input       = directory / "project.vpk";
    baseline.config.environment = directory / "sky.hdr";
    baseline.config.size        = {17, 13};
    baseline.config.path        = RenderPath::eReferencePathTracing;
    baseline.config.seed        = 42;
    baseline.config.camera      = RenderCamera {};
    baseline.graph              = GraphDefinition {{{"gain", "research.color_gain", {{"gain", 0.5}}}},
                                                   {{"scene.hdr", "gain.source"}},
                                                   {"gain.color"}};
    baseline.frames             = 3;
    baseline.warmup             = 1;
    baseline.timeStep           = 0.25f;
    baseline.save(file);
    const auto loaded = ExperimentDescription::load(file);
    require(loaded.config.input == baseline.config.input && loaded.config.environment == baseline.config.environment &&
                loaded.config.size == baseline.config.size && loaded.config.path == baseline.config.path &&
                loaded.config.seed == 42 && loaded.frames == 3 && loaded.warmup == 1 && loaded.timeStep == 0.25f &&
                loaded.graph->serialize() == baseline.graph->serialize() &&
                loaded.config.camera->view == baseline.config.camera->view,
            "Experiment round trip lost rendering/simulation state");
    auto data = nlohmann::json::parse(std::ifstream(file));
    require(data["version"] == 1 && data["input"] == "project.vpk" && data["environment"] == "sky.hdr",
            "Experiment writer did not preserve version-1 relative paths");
    const auto reject = [&](nlohmann::json invalid)
    {
        const auto bad = directory / "invalid.vexperiment";
        {
            std::ofstream stream(bad);
            stream << invalid;
        }
        try
        {
            ExperimentDescription::load(bad);
        }
        catch (const std::invalid_argument& error)
        {
            require(std::string_view(error.what()).contains("invalid.vexperiment"),
                    "Experiment error lost its input file location");
            return;
        }
        throw std::runtime_error("Invalid experiment input was accepted");
    };
    for (const auto* field : {"width", "height", "seed", "frames", "warmup"})
    {
        auto invalid   = data;
        invalid[field] = -1;
        reject(std::move(invalid));
    }
    for (const auto* field : {"width", "height", "seed"})
    {
        auto invalid   = data;
        invalid[field] = uint64_t(UINT32_MAX) + 1;
        reject(std::move(invalid));
    }
    for (const auto* field : {"width", "height", "frames", "time_step"})
    {
        auto invalid   = data;
        invalid[field] = 0;
        reject(std::move(invalid));
    }
    auto invalid      = data;
    invalid["warmup"] = UINT64_MAX;
    reject(invalid);
    invalid            = data;
    invalid["version"] = 2;
    reject(invalid);
    invalid                   = data;
    invalid["camera"]["view"] = std::vector<float>(16, 0);
    reject(invalid);
    invalid                  = data;
    invalid["camera"]["far"] = 0;
    reject(invalid);
    std::cout << "Experiment round trip, relative paths and invalid-input boundaries passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
