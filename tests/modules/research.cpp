#ifdef TEST_RESEARCH_CAMERA
#include <vultra/research/camera_track.hpp>
#endif
#ifdef TEST_RESEARCH_GRAPH
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/research/graph_report.hpp>
#endif
#ifdef TEST_RESEARCH_PROJECT
#include <vultra/research/research_project.hpp>
#endif
#ifdef TEST_RESEARCH_QUALITY
#include <vultra/research/quality.hpp>
#endif

#if defined(TEST_RESEARCH_GRAPH) || defined(TEST_RESEARCH_CAMERA) || defined(TEST_RESEARCH_PROJECT)
#include <nlohmann/json.hpp>
#endif

#ifdef TEST_RESEARCH_GRAPH
#include <array>
#include <span>
#endif
#include <filesystem>
#include <iostream>
#include <stdexcept>
#ifdef TEST_RESEARCH_QUALITY
#include <vector>
#endif

namespace
{
    using namespace vultra;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

#ifdef TEST_RESEARCH_QUALITY
    void quality()
    {
        const Image image {{32, 32}, std::vector<float>(32 * 32 * 4, 0.5f)};
        const auto  identical = evaluateFlip(image, image);
        require(identical.mean && *identical.mean < 1e-6 && identical.pixels == 32 * 32,
                "CPU quality feature changed identical-image error");
        auto changed              = image;
        changed.rgba[16 * 32 * 4] = 1;
        require(evaluateFlip(image, changed).mean.value() > 0, "FLIP did not detect a changed pixel");
        require(temporalError(image, image, changed, changed).value() == 0, "Matching temporal changes produced error");
        require(temporalError(image, image, image, changed).value() > 0,
                "Temporal quality did not detect a changed frame");
    }
#endif
#ifdef TEST_RESEARCH_CAMERA
    void camera(const std::filesystem::path& root)
    {
        CameraPose first;
        first.position = {0, 0, 2};
        auto last      = first;
        last.position  = {4, 0, 2};
        CameraTrack track {{{0, first}, {10, last}}};
        const auto  text = track.serialize();
        require(nlohmann::json::parse(text)["version"] == 1, "Camera format version changed");
        track.save(root / "camera.json");
        const auto restored = CameraTrack::load(root / "camera.json");
        require(restored.serialize() == text && restored.evaluate(5).position == glm::vec3(2, 0, 2) &&
                    restored.evaluate(10).position == last.position,
                "CPU camera playback or persistence changed");
        bool rejected = false;
        try
        {
            CameraTrack {{{0, first}, {0, last}}}.validate();
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected && restored.serialize() == text, "Invalid camera edit changed the previous track");
    }
#endif
#ifdef TEST_RESEARCH_PROJECT
    void project(const std::filesystem::path& root)
    {
        ProjectManifest manifest;
        manifest.mainScene     = "main.vscene";
        manifest.metadata      = R"({"project_note":42})";
        const auto      method = manifest.addAsset("method.vgraph");
        ResearchProject settings;
        settings.name       = "Module research";
        settings.methods    = {{"Base", method}};
        settings.comparison = method;
        settings.write(manifest);
        manifest.save(root / "research.vproject");
        const auto restored = ProjectManifest::load(root / "research.vproject");
        const auto metadata = nlohmann::json::parse(restored.metadata);
        require(metadata["project_note"] == 42 && ResearchProject::read(restored)->methods[0].graph == method,
                "Research metadata changed asset IDs or another owner's data");
        const auto before = manifest.metadata;
        settings.methods.push_back({"Base", method});
        bool rejected = false;
        try
        {
            settings.write(manifest);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected && manifest.metadata == before, "Invalid research metadata edit changed the project");
    }
#endif
#ifdef TEST_RESEARCH_GRAPH
    void graph()
    {
        Device      device;
        RenderGraph graph(device);
        const auto  color = graph.createTexture("result", colorTexture({16, 16}, VriFormat_RGBA32_SFLOAT));
        graph.addPass("Clear",
                      {{color, Usage::eColorWrite}},
                      [color](VriCommandBuffer* command, RenderGraph& active)
                      {
                          const float values[4] {0.25f, 0.5f, 0.75f, 1};
                          beginColorPass(active.device(), command, active.getTexture(color).view(), {16, 16}, values);
                          active.device().core.CmdEndRendering(command);
                      });
        graph.exportResource(color);
        graph.compile();
        Frame    frame(device);
        Profiler profiler(device);
        auto*    command = frame.begin();
        profiler.beginFrame(command);
        graph.execute(command, &profiler);
        profiler.resolve(command);
        frame.submitAndWait();
        profiler.collect();
        const auto pixels = readback(device, graph.getTexture(color));
        require(imagePixel(pixels, 8, 8) == std::array<float, 4> {0.25f, 0.5f, 0.75f, 1},
                "Reported graph produced an unexpected image");
        const GraphCapture capture {"result", "result.pfm", color};
        const auto report = nlohmann::json::parse(graphReport(graph, profiler.timings(), std::span(&capture, 1)));
        require(report["version"] == 1 && report["passes"].size() == 1 && report["events"][0]["pass"] == 0 &&
                    report["images"][0]["resource"] == color.index && report["images"][0]["producers"][0] == 0,
                "Graph report lost its image, pass or timing association");
    }
#endif
} // namespace

int main(int argc, char** argv)
try
{
    require(argc == 2, "Pass an unused research output directory");
    const std::filesystem::path root(argv[1]);
    require(!std::filesystem::exists(root), "Research output already exists");
    std::filesystem::create_directories(root);
#ifdef TEST_RESEARCH_QUALITY
    quality();
#endif
#ifdef TEST_RESEARCH_CAMERA
    camera(root);
#endif
#ifdef TEST_RESEARCH_PROJECT
    project(root);
#endif
#ifdef TEST_RESEARCH_GRAPH
    graph();
#endif
    std::cout << "Selected research facilities passed without an editor or language VM\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
