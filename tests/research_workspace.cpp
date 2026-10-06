#include "../editor/src/research_workspace.hpp"
#include "../examples/research/color_gain.hpp"

#include <vultra/core/base/stable_id.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace vultra;
    using Json = nlohmann::json;

    void require(bool value, const char* message)
    {
        if (!value)
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
            if (std::string_view(error.what()).find(text) == std::string_view::npos)
            {
                throw std::runtime_error("Expected workspace error containing " + std::string(text) +
                                         "; received: " + error.what());
            }
            return;
        }
        throw std::runtime_error("Invalid workspace edit was accepted");
    }

    Json readJson(const std::filesystem::path& file)
    {
        std::ifstream input(file);
        return Json::parse(input);
    }

    void writeJson(const std::filesystem::path& file, const Json& value)
    {
        std::ofstream output(file);
        output << value.dump(2);
        require(bool(output), "Cannot write workspace test fixture");
    }
} // namespace

int main()
try
{
    using namespace vultra;
    const auto directory =
        std::filesystem::path("build/.tmp") / ("research-workspace-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    Device          device;
    RenderingServer server(device);
    PassCatalog     catalog(device);
    catalog.add(research::colorGainDefinition());
    ResearchWorkspace workspace(device, server, catalog);
    ResearchDocument  document;
    document.project    = std::filesystem::absolute("resources/research.vproject");
    document.size       = {129, 97};
    document.definition = {{{"gain", "research.color_gain", {{"gain", 1}}}},
                           {{"scene.hdr", "gain.source"}},
                           {"gain.color", "scene.hdr", "scene.normal_roughness", "scene.depth"}};
    workspace.replace(document);
    Frame      frame(device);
    const auto render = [&]
    {
        workspace.prepareFrame();
        workspace.record(frame.begin());
        frame.submitAndWait();
        workspace.completeFrame();
        server.collectCompletedFrame();
        auto& graph = workspace.graph();
        return readback(device, graph.graph.getTexture(graph.rendererOutputs.color));
    };
    const auto baseline     = render();
    const auto sceneRid     = workspace.sceneRid();
    auto*      initialGraph = &workspace.graph();
    int        retired      = 0;
    const auto retire       = [&](auto&)
    {
        ++retired;
    };
    auto invalid                          = document;
    invalid.definition.edges.front().from = "scene.missing";
    invalid.settings.path                 = RenderPath::eNaiveForward;
    expectError(
        [&]
        {
            workspace.replace(invalid, retire);
        },
        "scene.missing");
    require(retired == 0 && &workspace.graph() == initialGraph && workspace.sceneRid() == sceneRid,
            "Failed graph edit retired active resources");
    require(render().rgba == baseline.rgba, "Failed graph edit changed the active renderer or image");
    invalid                                              = document;
    invalid.definition.passes.front().parameters["gain"] = 9;
    expectError(
        [&]
        {
            workspace.replace(invalid, retire);
        },
        "gain");
    require(render().rgba == baseline.rgba, "Failed parameter edit changed active image");
    invalid                    = document;
    invalid.definition.outputs = {"scene.depth"};
    expectError(
        [&]
        {
            workspace.replace(invalid, retire);
        },
        "First research output");
    require(retired == 0 && render().rgba == baseline.rgba, "Failed output edit stopped the last valid graph");
    document.definition.passes.front().parameters["gain"] = 0.5;
    workspace.replace(document, retire);
    const auto changed = render();
    require(retired == 1 && workspace.sceneRid() == sceneRid,
            "Parameter edit reuploaded geometry or skipped retirement");
    require(changed.rgba != baseline.rgba, "Applied gain did not change the rendered image");
    auto& active = workspace.graph();
    require(active.previews.size() == 5, "Marked outputs were lost");
    const auto gain = readback(device, active.graph.getTexture(active.instances.outputs[0].resource));
    const auto hdr  = readback(device, active.graph.getTexture(active.instances.outputs[1].resource));
    for (size_t i = 0; i < gain.rgba.size(); ++i)
    {
        const float expected = i % 4 == 3 ? hdr.rgba[i] : hdr.rgba[i] * 0.5f;
        require(std::isfinite(gain.rgba[i]) && std::abs(gain.rgba[i] - expected) < 0.002f,
                "Marked HDR output does not match the applied pass");
    }
    auto* parameterStorage = active.instances.passes.front().parameterValues.data();
    auto* passInstance     = active.instances.passes.front().instance.get();
    auto* outputTexture    = active.graph.getTexture(active.instances.outputs.front().resource).handle;
    workspace.setPassParameters("gain", {{"gain", 1}});
    require(render().rgba == baseline.rgba, "Live parameter update differs from a newly built graph");
    workspace.setPassParameters("gain", {{"gain", 0.5}});
    require(render().rgba == changed.rgba && &workspace.graph() == &active && workspace.sceneRid() == sceneRid &&
                active.instances.passes.front().parameterValues.data() == parameterStorage &&
                active.instances.passes.front().instance.get() == passInstance &&
                active.graph.getTexture(active.instances.outputs.front().resource).handle == outputTexture &&
                retired == 1,
            "Live parameter update recreated graph, pass, texture, geometry or parameter storage");
    expectError(
        [&]
        {
            workspace.setPassParameters("gain", {{"gain", 9}});
        },
        "gain: parameter out of range");
    expectError(
        [&]
        {
            workspace.setPassParameters("gain", {{"gaim", 1}});
        },
        "gain: unknown parameter");
    expectError(
        [&]
        {
            workspace.setPassParameters("missing", {{"gain", 1}});
        },
        "Unknown active pass");
    require(render().rgba == changed.rgba &&
                workspace.document().definition.serialize() == document.definition.serialize(),
            "Rejected live edit changed the active image or saved parameter values");
    workspace.setPassParameters("gain", {{"gain", 0.25}});
    document.definition.passes.front().parameters["gain"] = 0.25;
    workspace.camera().distance *= 0.8f;
    const auto savedImage = render();
    const auto saved      = directory / "project.vworkspace";
    workspace.save(saved);
    const auto reopened = ResearchDocument::load(saved);
    require(reopened.project == document.project && reopened.definition.serialize() == document.definition.serialize(),
            "Workspace save/load changed project or definition");
    workspace.replace(reopened, retire);
    require(render().rgba == savedImage.rgba, "Reopened workspace changed the camera/settings/rendered image");
    workspace.exportImages(directory / "capture");
    const auto manifest = readJson(directory / "capture/manifest.json");
    require(manifest.at("version") == 1 && manifest.at("outputs").size() == 5,
            "Capture lost marked output names or version");
    require(manifest.at("outputs")[1].at("port") == "gain.color", "Capture cannot associate the pass port");
    require(loadPng(directory / "capture/final.png").rgba == savedImage.rgba, "Captured final image changed");
    require(std::filesystem::file_size(directory / "capture/output_000.pfm") > 129 * 97 * 12,
            "Linear HDR capture payload is missing");
    expectError(
        [&]
        {
            workspace.exportImages(directory / "capture");
        },
        "new research capture");
    auto badFile       = readJson(saved);
    badFile["version"] = 2;
    writeJson(directory / "invalid.vworkspace", badFile);
    expectError(
        [&]
        {
            ResearchDocument::load(directory / "invalid.vworkspace");
        },
        "version 1");
    badFile                       = readJson(saved);
    badFile["camera"]["distance"] = 0;
    writeJson(directory / "invalid.vworkspace", badFile);
    expectError(
        [&]
        {
            ResearchDocument::load(directory / "invalid.vworkspace");
        },
        "camera");

    badFile              = readJson(saved);
    badFile["extent"][0] = -1;
    writeJson(directory / "invalid.vworkspace", badFile);
    expectError(
        [&]
        {
            ResearchDocument::load(directory / "invalid.vworkspace");
        },
        "positive integer");

    badFile                   = readJson(saved);
    badFile["node_positions"] = Json::array();
    writeJson(directory / "invalid.vworkspace", badFile);
    expectError(
        [&]
        {
            ResearchDocument::load(directory / "invalid.vworkspace");
        },
        "positions must be an object");
    badFile["node_positions"] = {{"pass:gain", {1000001, 0}}};
    writeJson(directory / "invalid.vworkspace", badFile);
    expectError(
        [&]
        {
            ResearchDocument::load(directory / "invalid.vworkspace");
        },
        "node position");

    auto emptyScene                = readJson("resources/scenes/research.vscene");
    emptyScene["root"]["children"] = Json::array();
    writeJson(directory / "empty.vscene", emptyScene);
    Json emptyProject {{"format", "vultra.project"},
                       {"version", 1},
                       {"main_scene", "empty.vscene"},
                       {"assets", Json::array()},
                       {"extensions", Json::array()},
                       {"scripts", Json::array()}};
    writeJson(directory / "empty.vproject", emptyProject);
    invalid                      = document;
    invalid.project              = directory / "empty.vproject";
    invalid.definition.outputs   = {"scene.depth"};
    const auto beforeSceneChange = render();
    expectError(
        [&]
        {
            workspace.replace(invalid, retire);
        },
        "First research output");
    require(workspace.sceneRid() == sceneRid && server.alive(sceneRid), "Failed project edit released active scene");
    require(render().rgba == beforeSceneChange.rgba, "Failed project edit changed the image");
    document.project    = directory / "empty.vproject";
    document.definition = {{}, {}, {"scene.hdr"}};
    workspace.replace(document, retire);
    require(workspace.sceneRid() != sceneRid && !server.alive(sceneRid),
            "Project replacement kept the old scene RID alive");
    require(render().rgba != beforeSceneChange.rgba, "Project replacement did not update the renderer");

    document.project = std::filesystem::absolute("resources/research_lighting.vproject");
    document.camera.reset();
    document.settings.ibl = false;
    workspace.replace(document, retire);
    const auto lightingImage     = render();
    const auto lightingRid       = workspace.sceneRid();
    auto*      lightingGraph     = &workspace.graph();
    auto*      lightingTexture   = lightingGraph->graph.getTexture(lightingGraph->rendererOutputs.hdr).handle;
    auto&      mesh              = **std::ranges::find_if(workspace.scene().root().children(),
                                                          [](const auto& child)
                                                          {
                                            return child->kind() == NodeKind::eMeshInstance;
                                                          });
    const auto originalTransform = mesh.localTransform();
    auto       transform         = originalTransform;
    transform[3].x += 0.6f;
    mesh.setLocalTransform(transform);
    require(render().rgba != lightingImage.rgba && workspace.sceneRid() == lightingRid &&
                &workspace.graph() == lightingGraph,
            "Workbench mesh transform notification did not update rendering without reimport");
    mesh.setLocalTransform(originalTransform);
    auto& rig      = workspace.scene().addChild(workspace.scene().root(), std::make_unique<Node>("Rig"));
    transform      = glm::mat4(1);
    transform[3].x = 0.6f;
    rig.setLocalTransform(transform);
    workspace.scene().reparent(mesh, rig);
    require(render().rgba != lightingImage.rgba && workspace.sceneRid() == lightingRid &&
                &workspace.graph() == lightingGraph,
            "Workbench mesh reparent failed to retain the upload and update inherited motion");
    workspace.scene().reparent(mesh, workspace.scene().root());
    workspace.scene().remove(rig);
    require(render().rgba == lightingImage.rgba, "Restored mesh parent retained a stale GPU transform");
    auto&      material           = *workspace.scene().materials().front();
    const auto importedParameters = material.parameters();
    auto       parameters         = importedParameters;
    parameters.baseRed            = 0;
    parameters.specularRoughness  = 0.9f;
    material.setParameters(parameters);
    const auto materialImage = render();
    require(materialImage.rgba != lightingImage.rgba && workspace.sceneRid() == lightingRid &&
                &workspace.graph() == lightingGraph &&
                lightingGraph->graph.getTexture(lightingGraph->rendererOutputs.hdr).handle == lightingTexture,
            "Workspace material edit did not affect textured rendering while retaining scene/graph resources");
    parameters.specularRoughness = -1;
    expectError(
        [&]
        {
            material.setParameters(parameters);
        },
        "Material");
    require(render().rgba == materialImage.rgba, "Rejected material edit changed the active workspace image");
    workspace.scene().save(directory / "materials.vscene");
    const auto reopenedScene = SceneTree::load(directory / "materials.vscene");
    require(reopenedScene.serialize() == workspace.scene().serialize(), "Material-edited scene changed on reopening");
    workspace.save(directory / "materials.vworkspace");
    const auto materialDocument = ResearchDocument::load(directory / "materials.vworkspace");
    require(materialDocument.sceneSnapshot == workspace.scene().serialize(),
            "Saved experiment lost its edited material resource state");
    material.setParameters(importedParameters);
    require(render().rgba == lightingImage.rgba, "Restoring material parameters changed the original image");
    workspace.replace(materialDocument, retire);
    require(render().rgba == materialImage.rgba, "Reopened material-edited experiment changed rendering");
    auto invalidSnapshot                            = materialDocument;
    auto data                                       = Json::parse(invalidSnapshot.sceneSnapshot);
    data["materials"][0]["parameters"]["roughness"] = -1;
    invalidSnapshot.sceneSnapshot                   = data.dump();
    const auto materialRid                          = workspace.sceneRid();
    expectError(
        [&]
        {
            invalidSnapshot.save(directory / "materials.vworkspace");
        },
        "Material");
    require(ResearchDocument::load(directory / "materials.vworkspace").sceneSnapshot == materialDocument.sceneSnapshot,
            "Rejected scene snapshot save damaged the previous workspace file");
    expectError(
        [&]
        {
            workspace.replace(invalidSnapshot, retire);
        },
        "Material");
    require(workspace.sceneRid() == materialRid && render().rgba == materialImage.rgba,
            "Invalid scene snapshot replaced the valid material-edited experiment");
    workspace.scene().materials().front()->setParameters(importedParameters);
    lightingGraph                 = &workspace.graph();
    lightingTexture               = lightingGraph->graph.getTexture(lightingGraph->rendererOutputs.hdr).handle;
    const auto currentLightingRid = workspace.sceneRid();
    const auto sunId              = NodeId {StableId::parse("91c7f6ec-2d3e-4083-9873-24a877fcfba1").value()};
    auto&      sun                = static_cast<LightNode&>(*workspace.scene().find(sunId));
    auto       settings           = sun.settings();
    settings.intensity            = 4;
    sun.setSettings(settings);
    require(render().rgba != lightingImage.rgba && workspace.sceneRid() == currentLightingRid &&
                &workspace.graph() == lightingGraph &&
                lightingGraph->graph.getTexture(lightingGraph->rendererOutputs.hdr).handle == lightingTexture,
            "Workspace scene light edit failed to reuse geometry and graph resources");
    auto& environment = static_cast<EnvironmentNode&>(*workspace.scene().find(workspace.scene().currentEnvironment()));
    environment.setSettings({0.25f});
    const auto dimEnvironment = render();
    environment.setSettings({1});
    const auto hdrEnvironment = render();
    require(dimEnvironment.rgba != hdrEnvironment.rgba && workspace.sceneRid() == currentLightingRid &&
                &workspace.graph() == lightingGraph &&
                lightingGraph->graph.getTexture(lightingGraph->rendererOutputs.hdr).handle == lightingTexture,
            "Environment intensity edit rebuilt scene/graph resources or failed to change the image");
    const auto radianceAsset = environment.radianceAsset();
    expectError(
        [&]
        {
            workspace.setEnvironment(environment.id(), {StableId::generate()});
        },
        "asset");
    expectError(
        [&]
        {
            workspace.setEnvironment(workspace.scene().root().id(), {});
        },
        "environment node");
    require(render().rgba == hdrEnvironment.rgba, "Rejected environment edit changed the active image");
    workspace.setEnvironment(environment.id(), {});
    const auto proceduralEnvironment = render();
    require(proceduralEnvironment.rgba != hdrEnvironment.rgba && workspace.sceneRid() == currentLightingRid &&
                &workspace.graph() == lightingGraph,
            "Environment asset replacement did not reuse the scene and graph");
    workspace.setEnvironment(environment.id(), radianceAsset);
    require(render().rgba == hdrEnvironment.rgba, "Restoring the HDR asset changed the original image");
    workspace.save(directory / "environment.vworkspace");
    auto environmentDocument     = ResearchDocument::load(directory / "environment.vworkspace");
    auto badEnvironment          = environmentDocument;
    data                         = Json::parse(environmentDocument.sceneSnapshot);
    data["current_environment"]  = data["root"]["id"];
    badEnvironment.sceneSnapshot = data.dump();
    expectError(
        [&]
        {
            workspace.replace(badEnvironment, retire);
        },
        "environment node");
    require(render().rgba == hdrEnvironment.rgba && workspace.sceneRid() == currentLightingRid,
            "Rejected environment selection replaced the active experiment");
    workspace.scene().setCurrentEnvironment({});
    workspace.replace(environmentDocument, retire);
    require(render().rgba == hdrEnvironment.rgba, "Saved experiment lost its authored environment");
    const auto sceneCameraImage = render();
    workspace.camera().yaw += 1;
    require(render().rgba == sceneCameraImage.rgba, "Orbit camera overrode the selected scene camera");
    workspace.scene().setCurrentCamera({});
    require(render().rgba != sceneCameraImage.rgba, "Clearing the scene camera did not restore code-driven control");
    std::cout
        << "Research workspace tests passed: live edits, failure recovery, stable geometry, persistence and captures\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
