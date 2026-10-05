#include "../examples/research/color_gain.hpp"

#include <vultra/main/experiment_session.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

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

    void expectError(const std::function<void()>& action, std::string_view text)
    {
        try
        {
            action();
        }
        catch (const std::exception& error)
        {
            require(std::string_view(error.what()).contains(text), "Unexpected experiment error");
            return;
        }
        throw std::runtime_error("Invalid experiment operation was accepted");
    }
} // namespace

int main()
try
{
    using namespace vultra;
    const auto directory = std::filesystem::absolute("build/.tmp/experiment-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    {
        std::ofstream model(directory / "quad.obj");
        model << "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n";
        require(bool(model), "Cannot write experiment model fixture");
    }
    ProjectManifest project;
    project.mainScene  = "scene.vscene";
    const auto modelId = project.addAsset("quad.obj");
    project.save(directory / "project.vproject");
    SceneTree scene(std::make_unique<Node>("Experiment"));
    scene.addChild(scene.root(), std::make_unique<MeshInstanceNode>("Quad", modelId));
    auto& camera = scene.addChild(scene.root(), std::make_unique<CameraNode>("Camera"));
    camera.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 3}));
    scene.setCurrentCamera(camera.id());
    auto& light = scene.addChild(scene.root(), std::make_unique<LightNode>("Light", RenderLightKind::ePoint));
    light.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 2}));
    scene.save(directory / project.mainScene);

    Device           device;
    ExperimentConfig config {.input = directory / "project.vproject", .size = {65, 49}};
    config.importOptions.cacheDirectory = directory / "cache";
    auto invalid                        = config;
    invalid.size.width                  = 0;
    expectError(
        [&]
        {
            ExperimentSession rejected(device, invalid);
        },
        "nonzero dimensions");
    ExperimentSession session(device, config);
    require(session.scene() && session.project() && session.projectRoot() == directory,
            "Session did not own its project scene");
    expectError(
        [&]
        {
            session.capture("final");
        },
        "no completed frame");
    require(session.render().frameIndex == 0, "Session frame indices do not start at zero");
    const auto baseline = session.capture("final");
    {
        auto pinnedConfig   = config;
        pinnedConfig.camera = session.camera();
        ExperimentSession pinned(device, pinnedConfig);
        pinned.render();
        auto* fixedTexture = pinned.output("final").handle;
        auto* cameraNode   = pinned.scene()->find(pinned.scene()->currentCamera());
        cameraNode->setLocalTransform(glm::translate(glm::mat4(1), {0.8f, 0, 3}));
        pinned.render();
        require(pinned.output("final").handle == fixedTexture && pinned.capture("final").rgba == baseline.rgba,
                "Pinned experiment camera did not retain its image across scene camera changes");
    }
    auto* color = session.output("final").handle;
    auto* hdr   = session.output("hdr").handle;
    require(!session.timings().empty(), "Session did not expose completed pass timings");
    auto& tree = *session.scene();
    auto& mesh = *tree.root().children()[0];
    mesh.setLocalTransform(glm::translate(glm::mat4(1), {0.4f, 0, 0}));
    require(session.render().frameIndex == 1 && session.capture("final").rgba != baseline.rgba,
            "Session did not synchronize changed node transforms");
    require(session.output("final").handle == color && session.output("hdr").handle == hdr,
            "Transform updates recreated graph textures");
    mesh.setLocalTransform(glm::mat4(1));
    session.render();
    require(session.capture("final").rgba == baseline.rgba, "Restoring a transform did not restore the image");
    auto& material = tree.addMaterial(std::make_unique<MaterialResource>("Material"));
    static_cast<MeshInstanceNode&>(mesh).setMaterial(0, material.assetId());
    auto parameters    = material.parameters();
    parameters.baseRed = 0.1f;
    material.setParameters(parameters);
    session.render();
    require(session.capture("final").rgba != baseline.rgba && session.output("final").handle == color,
            "Material changes did not reuse and update the existing graph");
    tree.removeMaterial(material);
    session.render();
    require(session.capture("final").rgba == baseline.rgba,
            "Clearing material overrides did not restore import values");

    session.passes().add(research::colorGainDefinition());
    GraphDefinition definition {{{"gain", "research.color_gain", {{"gain", 0.5}}}},
                                {{"scene.hdr", "gain.source"}},
                                {"gain.color", "scene.hdr"}};
    session.setGraph(definition);
    expectError(
        [&]
        {
            session.capture("final");
        },
        "no completed frame");
    session.render();
    const auto changed = session.capture("final");
    require(changed.rgba != baseline.rgba && session.markedOutputs().size() == 2, "Graph output was not applied");
    const auto processed = session.capture("hdr");
    const auto raw       = session.capture("scene.hdr");
    require(processed.rgba == session.capture("gain.color").rgba, "Processed HDR alias lost its marked output");
    for (size_t i = 0; i < raw.rgba.size(); ++i)
    {
        const auto expected = i % 4 == 3 ? raw.rgba[i] : raw.rgba[i] * 0.5f;
        require(std::isfinite(processed.rgba[i]) && std::abs(processed.rgba[i] - expected) < 0.002f,
                "Named scene.hdr output was confused with the processed HDR alias");
    }
    color                       = session.output("final").handle;
    auto badGraph               = definition;
    badGraph.edges.front().from = "scene.missing";
    expectError(
        [&]
        {
            session.setGraph(badGraph);
        },
        "scene.missing");
    require(session.output("final").handle == color && session.capture("final").rgba == changed.rgba,
            "Rejected graph replacement retired the last completed image");
    session.render();
    require(session.capture("final").rgba == changed.rgba, "Rejected graph replacement changed the active graph");
    expectError(
        [&]
        {
            session.capture("missing");
        },
        "Unknown experiment output");

    session.setPassParameters("gain", {{"gain", 1}});
    session.render();
    require(session.capture("final").rgba == baseline.rgba && session.output("final").handle == color,
            "Live pass parameter changes replaced graph resources or did not affect rendering");
    expectError(
        [&]
        {
            session.setPassParameters("gain", {{"gain", 9}});
        },
        "parameter out of range");
    expectError(
        [&]
        {
            session.setPassParameters("missing", {{"gain", 1}});
        },
        "Unknown experiment pass");
    session.render();
    require(session.capture("final").rgba == baseline.rgba, "Rejected live parameter edit changed rendering");
    session.setPassParameters("gain", {{"gain", 0.5}});
    session.render();
    static_cast<MeshInstanceNode&>(mesh).setModel({StableId::generate()});
    expectError(
        [&]
        {
            session.render();
        },
        "asset");
    require(session.capture("final").rgba == changed.rgba, "Failed scene import discarded the completed image");
    static_cast<MeshInstanceNode&>(mesh).setModel(modelId);
    session.render();
    require(session.capture("final").rgba == changed.rgba, "Failed scene import did not recover");

    auto& duplicate = tree.addChild(tree.root(), std::make_unique<MeshInstanceNode>("Duplicate", modelId));
    duplicate.setLocalTransform(glm::translate(glm::mat4(1), {-0.5f, 0, 0}));
    session.render();
    require(session.output("final").handle != color && session.capture("final").rgba != changed.rgba,
            "Mesh additions did not rebuild geometry and its dependent graph");
    tree.remove(duplicate);
    session.render();
    require(session.capture("final").rgba == changed.rgba, "Mesh removal did not restore the prior scene");
    tree.remove(mesh);
    session.render();
    const auto empty = session.capture("final");
    require(empty.rgba != changed.rgba && session.markedOutputs().size() == 2,
            "Zero-mesh rendering lost the active project pass");
    tree.addChild(tree.root(), std::make_unique<MeshInstanceNode>("Restored", modelId));
    session.render();
    require(session.capture("final").rgba == changed.rgba, "Zero-mesh recovery did not restore the rendered scene");
    savePng(session.capture("final"), directory / "final.png");
    std::cout << "Experiment session regressions passed: " << directory << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
