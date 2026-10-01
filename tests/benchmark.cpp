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
    vultra::BenchmarkMetadata metadata;
    metadata.experiment     = "test";
    metadata.sourceRevision = "test-revision";
    metadata.shaderHash     = "abcd";
    metadata.buildMode      = "test";
    metadata.width          = 320;
    metadata.height         = 200;
    metadata.warmupFrames   = 10;
    metadata.parameters     = {{"quoted", "a\"b"}};
    const auto directory = std::filesystem::path("build/.tmp") /
                           ("benchmark-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    capture.write(directory, metadata, device);
    require(read(directory / "manifest.json").find("\"quoted\": \"a\\\"b\"") != std::string::npos,
            "Benchmark metadata was not escaped");
    require(read(directory / "summary.csv").find("\"frame.total\",4,25,25,40,10,40") != std::string::npos,
            "Benchmark percentile or median is wrong");
    require(read(directory / "summary.csv").find(R"("pass.tri,""angle.cpu_total")") != std::string::npos,
            "Summary metric name was not CSV-escaped");
    require(read(directory / "passes.csv").find(R"("tri,""angle",1,0.25,0.75,2,0.5,1.5)") != std::string::npos,
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
}
