#include <vultra/drivers/profiling/benchmark.hpp>

#include <array>
#include <chrono>
#include <fstream>
#include <iterator>
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

    std::string read(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        require(bool(file), "Benchmark output is missing");
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
} // namespace

int main()
{
    vultra::BenchmarkCapture capture;
    for (uint64_t i = 0; i < 4; ++i)
    {
        vultra::FrameTiming frame {};
        frame.frameIndex = i + 10;
        frame.totalMs    = double((i + 1) * 10);
        frame.gpuMs      = 2;
        const std::array passes {vultra::PassTiming {"tri,\"angle", 1, 2, 0.25, 0.5}};
        capture.add(frame, passes);
    }
    require(capture.size() == 4, "Benchmark lost a frame");
    VriDeviceDesc device {};
    device.adapter.vendorId    = 42;
    device.hasTimestampQueries = true;
    device.enabledFeatures     = 17;
    vultra::BenchmarkMetadata metadata;
    metadata.experiment     = "test";
    metadata.sourceRevision = "test-revision";
    metadata.shaderHash     = "abcd";
    metadata.buildMode      = "test";
    metadata.presentMode    = "none";
    metadata.width          = 320;
    metadata.height         = 200;
    metadata.warmupFrames   = 10;
    metadata.parameters     = {{"quoted", "a\"b"}};
    const auto directory = std::filesystem::path("build/.tmp") /
                           ("benchmark-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    capture.write(directory, metadata, device);
    const auto manifest = read(directory / "manifest.json");
    require(manifest.find("\"version\": 1") != std::string::npos &&
                manifest.find("\"enabled_features\": 17") != std::string::npos &&
                manifest.find("\"present_mode\": \"none\"") != std::string::npos,
            "Benchmark lost its format version or enabled device capabilities");
    require(read(directory / "manifest.json").find("\"quoted\": \"a\\\"b\"") != std::string::npos,
            "Benchmark metadata was not escaped");
    require(read(directory / "summary.csv").find("\"frame.total\",4,25,25,40,10,40") != std::string::npos,
            "Benchmark percentile or median is wrong");
    require(read(directory / "summary.csv").find(R"("pass.tri,""angle.cpu_total")") != std::string::npos,
            "Summary metric name was not CSV-escaped");
    require(read(directory / "passes.csv").find(R"("tri,""angle",1,0.25,0.75,2,0.5,1.5,0,,0)") != std::string::npos,
            "Pass barrier and command time were not separated");
    bool rejected = false;
    try
    {
        capture.write(directory, metadata, device);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "Benchmark overwrote an existing run");

    vultra::BenchmarkCapture checked;
    const auto               checkedDirectory = directory / "checked";
    require(!checked.writeExpected(checkedDirectory, metadata, device) && !std::filesystem::exists(checkedDirectory),
            "An empty checked run created output");
    vultra::FrameTiming frame {};
    frame.gpuMs = 2;
    const std::array passes {vultra::PassTiming {"stage", 1, 2, 0.25, 0.5}};
    require(checked.addExpected(frame, passes, false).has_value(), "Cannot add checked benchmark frame");
    frame.frameIndex = 2;
    require(!checked.addExpected(frame, passes, true) && checked.size() == 1, "Nonconsecutive frame was stored");
    frame.frameIndex = 1;
    frame.gpuMs.reset();
    require(checked.addExpected(frame, passes, true).has_value(), "Cannot add frame after rejection");
    require(checked.writeExpected(checkedDirectory / "", metadata, device).has_value(),
            "Cannot write checked benchmark");
    require(!checked.writeExpected(checkedDirectory, metadata, device), "Checked writer overwrote an existing run");
    const auto passRows = read(checkedDirectory / "passes.csv");
    require(passRows.find(R"(0,"stage",1,0.25,0.75,,,,0,,0)") != std::string::npos &&
                passRows.find(R"(1,"stage",1,0.25,0.75,2,0.5,1.5,0,,0)") != std::string::npos,
            "Pass timestamp availability must be independent of the frame timestamp");
}
