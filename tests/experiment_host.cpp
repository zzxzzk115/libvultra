#include "../examples/research/color_gain.hpp"

#include <vultra/api/experiment_bridge.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/experiment_host.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

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
    const auto directory = std::filesystem::absolute("build/.tmp/experiment-host-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    {
        std::ofstream model(directory / "quad.obj");
        model << "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n";
        require(bool(model), "Cannot write experiment model");
    }
    SceneTree       tree(std::make_unique<Node>("Experiment"));
    ProjectManifest project;
    project.mainScene = "scene.vscene";
    const auto model  = project.addAsset("quad.obj");
    tree.addChild(tree.root(), std::make_unique<MeshInstanceNode>("Quad", model));
    tree.save(directory / project.mainScene);
    {
        std::ofstream script(directory / "motion.lua");
        script << "local Motion = {}\n"
                  "function Motion:_ready() self.time = 0 end\n"
                  "function Motion:_process(delta)\n"
                  "  self.time = self.time + delta\n"
                  "  self.node:get_child(0):set_position(self.time, 0, 0)\n"
                  "end\n"
                  "function Motion:_editor_gui() error('Unexpected offline GUI callback') end\n"
                  "return Motion\n";
        require(bool(script), "Cannot write experiment script");
    }
    project.scripts.push_back({ScriptModule::Language::eLua, "motion.lua", {}, std::nullopt});
    project.save(directory / "project.vproject");
    Device      device;
    PassCatalog catalog(device);
    catalog.add(research::colorGainDefinition());
    const AssetImportOptions imports {.cacheDirectory = directory / "cache"};
    ExperimentHost           host(device, catalog, std::filesystem::current_path(), imports);
    ExperimentHost           foreign(device, catalog, std::filesystem::current_path(), imports);
    const auto&              api = experimentApi();
    host.recordError("UTF-8", std::string(696, 'x') + "\xE9\x94\x99\xE8\xAF\xAF");
    require(host.lastError().ends_with("\xE9\x94\x99"), "Truncated ABI error split a UTF-8 code point");
    require(api.version == 1 && api.struct_size == sizeof(VultraExperimentApi),
            "Experiment ABI changed its version/layout");
    const auto input   = (directory / "project.vproject").string();
    uint64_t   session = 0;
    require(api.open(nullptr, input.data(), input.size(), 65, 49, 0, 0.25f, nullptr, 0, 0, &session) ==
                    VULTRA_STATUS_INVALID_ARGUMENT &&
                session == 0,
            "Null experiment context was accepted");
    require(api.open(&host, input.data(), input.size(), 65, 49, 0, 0, nullptr, 0, 0, &session) ==
                    VULTRA_STATUS_INVALID_ARGUMENT &&
                session == 0,
            "Invalid time step published a session");
    require(api.open(&host, input.data(), input.size(), 65, 49, 0, 0.25f, nullptr, 0, 0, &session) == VULTRA_STATUS_OK,
            "Generated experiment API did not create the scripted session");
    VultraExperimentProgress progress {};
    require(api.progress(&foreign, session, &progress) == VULTRA_STATUS_INVALID_ARGUMENT &&
                api.progress(&host, tree.root().id().value, &progress) == VULTRA_STATUS_INVALID_ARGUMENT &&
                api.progress(&host, (uint64_t(1) << 56) | 1, &progress) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Foreign context/object/resource identity was accepted as an experiment session");
    VultraExperimentImageInfo info {};
    require(api.image_info(&host, session, "final", 5, &info) == VULTRA_STATUS_ERROR,
            "Uninitialized experiment output was reported as an image");
    require(api.step(&host, session, 3) == VULTRA_STATUS_OK &&
                api.progress(&host, session, &progress) == VULTRA_STATUS_OK && progress.frames == 3 &&
                progress.seconds == 0.75 && progress.timeStep == 0.25f,
            "Fixed-step script progress is not tied to completed rendering");
    require(api.image_info(&host, session, "final", 5, &info) == VULTRA_STATUS_OK && info.width == 65 &&
                info.height == 49 && info.floatCount == 65 * 49 * 4,
            "Generated image descriptor does not match its texture");
    std::vector<float> pixels(info.floatCount, -1);
    require(api.read_image(&host, session, "final", 5, pixels.data(), pixels.size() - 1) ==
                    VULTRA_STATUS_INVALID_ARGUMENT &&
                std::ranges::all_of(pixels,
                                    [](float value)
                                    {
                                        return value == -1;
                                    }),
            "Wrong-sized readback modified the caller's buffer");
    require(api.read_image(&host, session, "final", 5, pixels.data(), pixels.size()) == VULTRA_STATUS_OK,
            "Generated float-array marshalling failed");
    const char* data   = nullptr;
    uint64_t    length = 0;
    require(api.scene_snapshot(&host, session, &data, &length) == VULTRA_STATUS_OK,
            "Generated experiment snapshot did not expose UTF-8 JSON");
    auto snapshot = SceneTree::parse({data, size_t(length)});
    require(snapshot.root().children()[0]->localTransform()[3].x == 0.75f,
            "Script updates did not use the configured fixed time step");
    require(api.step(&host, session, 0) == VULTRA_STATUS_INVALID_ARGUMENT &&
                api.last_error(&host, &data, &length) == VULTRA_STATUS_OK &&
                std::string_view(data, length).contains("positive non-overflowing"),
            "Experiment failure lost its operation diagnostic");
    require(api.close(&host, session) == VULTRA_STATUS_OK &&
                api.close(&host, session) == VULTRA_STATUS_INVALID_ARGUMENT &&
                api.progress(&host, session, &progress) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Released experiment identity stayed usable");
    const auto replacement = host.open(input, 65, 49, 0, 0.25f, "");
    require(replacement != session, "Experiment identity was reused after release");
    host.step(replacement, 3);
    std::vector<float> repeated(info.floatCount);
    host.readImage(replacement, "final", repeated);
    require(repeated == pixels, "Fresh scripted experiment did not reproduce the GPU image");
    host.close(replacement);
    std::cout << "Experiment host/ABI regressions passed: " << directory << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
