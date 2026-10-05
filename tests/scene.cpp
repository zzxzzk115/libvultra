#include <vultra/assets/project_manifest.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_api.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/scene/scene_render_state.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
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

    template<typename Callback>
    void requireFailure(Callback&& callback, const char* message)
    {
        bool rejected = false;
        try
        {
            callback();
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, message);
    }

    std::string readText(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    void testMaterialResources(const std::filesystem::path& root)
    {
        using namespace vultra;
        ProjectManifest project;
        const auto      model = project.addAsset("first.obj");
        SceneTree       tree(std::make_unique<Node>("Materials"));
        auto&           first = static_cast<MeshInstanceNode&>(
            tree.addChild(tree.root(), std::make_unique<MeshInstanceNode>("First", model)));
        auto& second = static_cast<MeshInstanceNode&>(
            tree.addChild(tree.root(), std::make_unique<MeshInstanceNode>("Second", model)));
        auto& red             = tree.addMaterial(std::make_unique<MaterialResource>("Red"));
        auto  parameters      = red.parameters();
        parameters.baseGreen  = 0;
        parameters.baseBlue   = 0;
        parameters.coatWeight = 0.2f;
        red.setParameters(parameters);
        first.setMaterial(0, red.assetId());
        second.setMaterial(0, red.assetId());
        const auto duplicate = sceneDuplicateMesh(tree, first.id(), tree.root().id());
        require(sceneMeshMaterial(tree, duplicate, 0) == red.id(), "Mesh duplication lost its shared material");
        tree.remove(*tree.find(duplicate));
        AssetImportOptions options;
        options.cache                = false;
        options.textures.compression = TextureCompression::eNone;
        auto imported                = importScene(tree, project, root, options);
        require(imported.scene.materials.size() == 2 && imported.textures.images.size() == 2 &&
                    imported.scene.materials[0].baseColor == imported.scene.materials[1].baseColor &&
                    imported.scene.materials[0].coatWeight == 0.2f,
                "Shared material did not apply to both instances with shared prepared textures");
        auto& blue          = tree.addMaterial(std::make_unique<MaterialResource>("Blue"));
        parameters.baseRed  = 0;
        parameters.baseBlue = 1;
        blue.setParameters(parameters);
        second.setMaterial(0, blue.assetId());
        imported = importScene(tree, project, root, options);
        require(imported.scene.materials[0].baseColor.r == 1 && imported.scene.materials[0].baseColor.b == 0 &&
                    imported.scene.materials[1].baseColor.r == 0 && imported.scene.materials[1].baseColor.b == 1 &&
                    imported.textures.materials[0] == imported.textures.materials[1],
                "Per-instance overrides changed another instance or duplicated texture bindings");
        const auto serialized = tree.serialize();
        auto       copy       = SceneTree::parse(serialized);
        require(copy.serialize() == serialized && copy.findMaterial(red.assetId())->id() != red.id() &&
                    sceneMeshMaterial(copy, copy.find(first.idInScene())->id(), 0) ==
                        copy.findMaterial(red.assetId())->id(),
                "Material persistence lost shared stable identity or retained a runtime ObjectId");
        auto invalid = nlohmann::json::parse(serialized);
        invalid["materials"].push_back(invalid["materials"][0]);
        requireFailure(
            [&]
            {
                SceneTree::parse(invalid.dump());
            },
            "Duplicate material asset ID was accepted");
        invalid = nlohmann::json::parse(serialized);
        invalid.erase("materials");
        requireFailure(
            [&]
            {
                SceneTree::parse(invalid.dump());
            },
            "Missing required materials table was accepted");
        invalid                                            = nlohmann::json::parse(serialized);
        invalid["materials"][0]["parameters"]["roughness"] = -1;
        requireFailure(
            [&]
            {
                SceneTree::parse(invalid.dump());
            },
            "Invalid saved material parameters were accepted");
        second.setMaterial(7, blue.assetId());
        requireFailure(
            [&]
            {
                importScene(tree, project, root, options);
            },
            "Missing imported material slot was accepted");
        second.setMaterial(7, {});
        second.setMaterial(0, {StableId::generate()});
        requireFailure(
            [&]
            {
                tree.serialize();
            },
            "Dangling material reference was saved");
        second.setMaterial(0, red.assetId());
        tree.removeMaterial(red);
        require(first.materialOverrides().empty() && second.materialOverrides().empty(),
                "Removing a shared material left dangling mesh references");
    }

    void testSceneImport(const std::filesystem::path& root)
    {
        const auto     firstPath  = root / "first.obj";
        const auto     secondPath = root / "second.obj";
        constexpr auto triangle   = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
        std::ofstream(firstPath) << triangle;
        std::ofstream(secondPath) << triangle;

        vultra::ProjectManifest project;
        const auto              first  = project.addAsset("first.obj");
        const auto              second = project.addAsset("second.obj");
        vultra::SceneTree       scene(std::make_unique<vultra::Node>("World"));
        scene.root().setLocalTransform(glm::translate(glm::mat4(1), {1, 0, 0}));
        auto& group = scene.addChild(scene.root(), std::make_unique<vultra::Node>("Group"));
        group.setLocalTransform(glm::translate(glm::mat4(1), {0, 2, 0}));
        scene.addChild(group, std::make_unique<vultra::MeshInstanceNode>("First", first));
        auto& reflected = scene.addChild(group, std::make_unique<vultra::MeshInstanceNode>("Reflected", first));
        reflected.setLocalTransform(glm::scale(glm::mat4(1), {-1, 1, 1}));
        auto& other = scene.addChild(group, std::make_unique<vultra::MeshInstanceNode>("Other", second));
        other.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 3}));

        vultra::AssetImportOptions options;
        options.cache                = false;
        options.textures.compression = vultra::TextureCompression::eNone;
        std::vector<vultra::SceneMeshInstance> instances;
        auto imported = vultra::importScene(scene, project, root, options, &instances);
        require(imported.scene.vertices.size() == 9 && imported.scene.indices.size() == 9 &&
                    imported.scene.primitives.size() == 3,
                "Scene mesh instances were not composed");
        require(vultra::sceneMeshTopologyMatches(scene, instances), "Imported mesh topology did not match its scene");
        group.setLocalTransform(glm::translate(glm::mat4(1), {0, 3, 0}));
        require(vultra::sceneMeshTopologyMatches(scene, instances), "A transform edit changed mesh topology");
        group.setLocalTransform(glm::translate(glm::mat4(1), {0, 2, 0}));
        static_cast<vultra::MeshInstanceNode&>(other).setModel(first);
        require(!vultra::sceneMeshTopologyMatches(scene, instances), "A model swap kept stale GPU geometry");
        static_cast<vultra::MeshInstanceNode&>(other).setModel(second);
        auto removed = scene.remove(other);
        require(!vultra::sceneMeshTopologyMatches(scene, instances), "A removed mesh kept stale GPU geometry");
        scene.addChild(group, std::move(removed));
        require(vultra::sceneMeshTopologyMatches(scene, instances), "Reattached mesh changed import order");
        auto& added = scene.addChild(group, std::make_unique<vultra::MeshInstanceNode>("Added", first));
        require(!vultra::sceneMeshTopologyMatches(scene, instances), "An added mesh was not detected");
        scene.remove(added);
        require(vultra::sceneMeshTopologyMatches(scene, instances), "Removing a new mesh did not restore topology");
        require(instances.size() == 3 && instances[1].node == reflected.idInScene() && instances[1].model == first &&
                    instances[1].firstPrimitive == 1 && instances[1].primitiveCount == 1 &&
                    instances[1].bakedTransform[0][0] == -1 && instances[1].bakedTransform[3][0] == 1 &&
                    instances[1].bakedTransform[3][1] == 2,
                "Imported instance range lost its scene node or baked transform");
        require(imported.scene.materials.size() == 3 && imported.textures.materials.size() == 3 &&
                    imported.textures.images.size() == 4,
                "Instance parameters must be independent while prepared textures remain shared");
        require(imported.scene.primitives[0].material == 0 && imported.scene.primitives[1].material == 1 &&
                    imported.scene.primitives[2].material == 2,
                "Scene material offsets are incorrect");
        require(imported.scene.indices[3] == imported.scene.indices[0] + 3 &&
                    imported.scene.indices[4] == imported.scene.indices[2] + 3 &&
                    imported.scene.indices[5] == imported.scene.indices[1] + 3,
                "Reflected mesh winding was not corrected");
        require(glm::length(imported.scene.center - glm::vec3(1, 2.5f, 1.5f)) < 0.0001f &&
                    std::abs(imported.scene.radius - 0.5f * std::sqrt(14.0f)) < 0.0001f,
                "Composed scene bounds ignore node transforms");

        vultra::SceneTree                      empty(std::make_unique<vultra::Node>("Empty"));
        std::vector<vultra::SceneMeshInstance> emptyInstances = instances;
        auto emptyImport = vultra::importScene(empty, project, root, options, &emptyInstances);
        require(emptyInstances.empty() && emptyImport.scene.primitives.empty() &&
                    emptyImport.scene.materials.size() == 1 && emptyImport.textures.materials.size() == 1 &&
                    vultra::sceneMeshTopologyMatches(empty, emptyInstances),
                "Removing every mesh did not produce a renderable empty scene");

        reflected.setLocalTransform(glm::scale(glm::mat4(1), {0, 1, 1}));
        requireFailure(
            [&]
            {
                vultra::importScene(scene, project, root, options);
            },
            "Singular mesh transform was accepted");
    }

    void testSceneNotifications()
    {
        using namespace vultra;
        SceneTree tree(std::make_unique<Node>("Notifications"));
        auto&     group = tree.addChild(tree.root(), std::make_unique<Node>("Group"));
        auto&     mesh  = static_cast<MeshInstanceNode&>(
            tree.addChild(group, std::make_unique<MeshInstanceNode>("Mesh", AssetId {StableId::generate()})));
        auto& camera = static_cast<CameraNode&>(tree.addChild(group, std::make_unique<CameraNode>("Camera")));
        auto& light  = static_cast<LightNode&>(
            tree.addChild(group, std::make_unique<LightNode>("Light", RenderLightKind::ePoint)));
        auto& environment =
            static_cast<EnvironmentNode&>(tree.addChild(group, std::make_unique<EnvironmentNode>("Environment")));
        auto& material = tree.addMaterial(std::make_unique<MaterialResource>("Material"));
        tree.setCurrentCamera(camera.id());
        tree.setCurrentEnvironment(environment.id());
        SceneRenderState first;
        SceneRenderState second;
        require(first.update(tree, {100, 50}) && second.update(tree, {100, 50}),
                "One scene consumer consumed another consumer's initial notification");
        const auto before = tree.changes();
        group.setName(group.name());
        group.setLocalTransform(group.localTransform());
        mesh.setModel(mesh.model());
        mesh.setMaterial(0, {});
        material.setParameters(material.parameters());
        camera.setSettings(camera.settings());
        light.setSettings(light.settings());
        environment.setSettings(environment.settings());
        environment.setRadianceAsset(environment.radianceAsset());
        tree.setCurrentCamera(camera.id());
        tree.setCurrentEnvironment(environment.id());
        tree.reparent(mesh, group);
        require(tree.changes() == before && !first.update(tree, {100, 50}),
                "Idempotent writes or reparent invalidated an unchanged frame");
        group.setName("Renamed");
        require(tree.changes().metadata != before.metadata && !first.update(tree, {100, 50}),
                "A metadata edit invalidated rendering state");
        auto parameters    = material.parameters();
        parameters.baseRed = 0.5f;
        material.setParameters(parameters);
        mesh.setMaterial(0, material.assetId());
        require(tree.changes().materials != before.materials && !first.update(tree, {100, 50}),
                "A material edit rebuilt the camera/light snapshot");
        const auto accepted = tree.changes();
        requireFailure(
            [&]
            {
                camera.setSettings({0, 0.1f, 10});
            },
            "Invalid camera edit was accepted");
        requireFailure(
            [&]
            {
                environment.setSettings({-1});
            },
            "Invalid environment edit was accepted");
        parameters.baseRed = -1;
        requireFailure(
            [&]
            {
                material.setParameters(parameters);
            },
            "Invalid material edit was accepted");
        requireFailure(
            [&]
            {
                tree.reparent(group, mesh);
            },
            "Scene cycle was accepted");
        require(tree.changes() == accepted, "Rejected edits emitted successful change notifications");
        const auto* lights = first.lights.data();
        environment.setSettings({2});
        require(first.update(tree, {100, 50}) && second.update(tree, {100, 50}) && first.environmentIntensity == 2 &&
                    second.environmentIntensity == 2 && first.lights.data() == lights,
                "Environment edits rebuilt lights or were lost between independent consumers");
        group.setLocalTransform(glm::translate(glm::mat4(1), {1, 2, 3}));
        require(first.update(tree, {100, 50}) && second.update(tree, {100, 50}) &&
                    first.lights.front().position == glm::vec3(1, 2, 3) &&
                    second.lights.front().position == first.lights.front().position,
                "Parent transform notification did not reach all scene consumers");
        auto projection        = camera.settings();
        projection.verticalFov = 0.8f;
        camera.setSettings(projection);
        auto lighting      = light.settings();
        lighting.intensity = 7;
        light.setSettings(lighting);
        require(first.update(tree, {100, 50}) && first.lights.front().intensity == 7 &&
                    tree.changes().camera != before.camera && tree.changes().lighting != before.lighting,
                "Camera/light fields did not notify their rendering domains");
        require(first.update(tree, {50, 100}), "Extent change retained the old camera projection");

        auto       detached = tree.remove(group);
        const auto removed  = tree.changes();
        mesh.setModel({StableId::generate()});
        group.setLocalTransform(glm::mat4(1));
        camera.setSettings({1, 0.1f, 10});
        require(tree.changes() == removed && first.update(tree, {100, 50}) && !first.camera && first.lights.empty(),
                "Detached subtree kept notifying its former owner");
        tree.addChild(tree.root(), std::move(detached));
        SceneTree  moved(std::move(tree));
        const auto movedBefore = moved.changes();
        mesh.setLocalTransform(glm::translate(glm::mat4(1), {4, 0, 0}));
        parameters          = material.parameters();
        parameters.baseBlue = 0.4f;
        material.setParameters(parameters);
        require(moved.changes().transforms != movedBefore.transforms &&
                    moved.changes().materials != movedBefore.materials,
                "Scene move broke attached node/resource notification storage");
        SceneTree destination(std::make_unique<Node>("Destination"));
        auto oldDetached = destination.remove(destination.addChild(destination.root(), std::make_unique<Node>("Old")));
        destination      = std::move(moved);
        const auto assignedBefore = destination.changes();
        light.setSettings({1, 1, 1, 3, 10, 0.35f, 0.6f});
        oldDetached->setName("Still detached");
        require(destination.changes().lighting != assignedBefore.lighting &&
                    destination.changes().metadata == assignedBefore.metadata,
                "Scene move assignment redirected notifications or retained a detached borrower");
        auto reopened = SceneTree::parse(destination.serialize());
        require(first.update(reopened, {100, 50}), "Reloaded tree was confused with a reused revision number");
    }

    void testEnvironmentNodes()
    {
        using namespace vultra;
        ProjectManifest project;
        const auto      hdr   = project.addAsset("sky.hdr");
        const auto      model = project.addAsset("mesh.glb");
        project.environment   = hdr;
        SceneTree  tree(std::make_unique<Node>("World"));
        auto&      group = tree.addChild(tree.root(), std::make_unique<Node>("Environment group"));
        const auto id    = sceneCreateEnvironment(tree, group.id(), "Studio");
        require(tree.currentEnvironment().value == 0 &&
                    sceneEnvironmentPath(tree, project, "assets") == "assets/sky.hdr",
                "Unselected environment node replaced the project preset");
        sceneSetCurrentEnvironment(tree, id);
        sceneSetEnvironmentSettings(tree, id, {2});
        SceneRenderState state;
        state.update(tree, {100, 50});
        require(state.environmentIntensity == 2 && sceneEnvironmentPath(tree, project, "assets").empty(),
                "Selected procedural environment inherited the project HDR map");
        sceneSetEnvironmentAsset(tree, project, id, hdr.value.toString());
        const auto serialized = tree.serialize();
        auto       copy       = SceneTree::parse(serialized);
        copy.validateAssets(project);
        require(copy.serialize() == serialized && copy.currentEnvironment() != id &&
                    sceneEnvironmentSettings(copy, copy.currentEnvironment()).intensity == 2 &&
                    sceneEnvironmentPath(copy, project, "assets") == "assets/sky.hdr",
                "Environment persistence lost stable selection, parameters or asset identity");
        requireFailure(
            [&]
            {
                sceneSetEnvironmentSettings(tree, id, {-1});
            },
            "Negative environment intensity was accepted");
        requireFailure(
            [&]
            {
                sceneSetEnvironmentAsset(tree, project, id, model.value.toString());
            },
            "Non-HDR environment asset was accepted");
        requireFailure(
            [&]
            {
                sceneSetEnvironmentAsset(tree, project, id, StableId::generate().toString());
            },
            "Unknown environment asset was accepted");
        require(sceneEnvironmentPath(tree, project, "assets") == "assets/sky.hdr",
                "Rejected environment edit changed active state");
        requireFailure(
            [&]
            {
                tree.setCurrentEnvironment(group.id());
            },
            "Non-environment node became current environment");
        auto invalid = nlohmann::json::parse(serialized);
        invalid.erase("current_environment");
        requireFailure(
            [&]
            {
                SceneTree::parse(invalid.dump());
            },
            "Missing environment selection was accepted");
        invalid                        = nlohmann::json::parse(serialized);
        invalid["current_environment"] = group.idInScene().value.toString();
        requireFailure(
            [&]
            {
                SceneTree::parse(invalid.dump());
            },
            "Wrong persisted environment type was accepted");
        invalid                        = nlohmann::json::parse(serialized);
        invalid["current_environment"] = StableId::generate().toString();
        requireFailure(
            [&]
            {
                SceneTree::parse(invalid.dump());
            },
            "Missing selected environment was accepted");
        invalid                                                                   = nlohmann::json::parse(serialized);
        invalid["root"]["children"][0]["children"][0]["environment"]["intensity"] = -1;
        requireFailure(
            [&]
            {
                SceneTree::parse(invalid.dump());
            },
            "Invalid persisted intensity was accepted");
        auto& environment = static_cast<EnvironmentNode&>(*tree.find(id));
        environment.setRadianceAsset(model);
        requireFailure(
            [&]
            {
                tree.validateAssets(project);
            },
            "Persisted non-HDR environment asset was accepted");
        environment.setRadianceAsset(hdr);
        tree.remove(group);
        state.update(tree, {100, 50});
        require(tree.currentEnvironment().value == 0 && state.environmentIntensity == 1 &&
                    sceneEnvironmentPath(tree, project, "assets") == "assets/sky.hdr",
                "Removing the selected environment's ancestor did not restore the project preset");
    }

    void testRenderNodes()
    {
        using namespace vultra;
        SceneTree tree(std::make_unique<Node>("World"));
        auto&     group = tree.addChild(tree.root(), std::make_unique<Node>("Camera rig"));
        group.setLocalTransform(glm::translate(glm::mat4(1), {1, 2, 0}) * glm::scale(glm::mat4(1), {2, 3, 4}));
        auto& camera = static_cast<CameraNode&>(tree.addChild(group, std::make_unique<CameraNode>("Camera")));
        camera.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 3}));
        tree.setCurrentCamera(camera.id());
        SceneRenderState state;
        state.update(tree, {100, 50});
        require(state.camera &&
                    glm::length(glm::vec3(glm::inverse(state.camera->view)[3]) - glm::vec3(1, 2, 12)) < 0.0001f,
                "Camera ignored parent translation/scale");
        require(std::abs(glm::length(glm::vec3(state.camera->view[0])) - 1) < 0.0001f,
                "Camera inherited scale instead of using orthonormal axes");
        const auto nearDepth = state.camera->projection * glm::vec4(0, 0, -camera.settings().nearPlane, 1);
        const auto farDepth  = state.camera->projection * glm::vec4(0, 0, -camera.settings().farPlane, 1);
        require(std::abs(nearDepth.z / nearDepth.w) < 0.00001f && std::abs(farDepth.z / farDepth.w - 1) < 0.00001f,
                "Scene camera did not preserve VRI zero-to-one clip depth");
        const auto persisted    = nlohmann::json::parse(tree.serialize());
        auto       invalidScene = persisted;
        invalidScene.erase("current_camera");
        requireFailure(
            [&]
            {
                SceneTree::parse(invalidScene.dump());
            },
            "Old scene structure was accepted by a compatibility reader");
        invalidScene                   = persisted;
        invalidScene["current_camera"] = group.idInScene().value.toString();
        requireFailure(
            [&]
            {
                SceneTree::parse(invalidScene.dump());
            },
            "Persisted current camera accepted a non-camera node");
        invalidScene                                                         = persisted;
        invalidScene["root"]["children"][0]["children"][0]["camera"]["near"] = -1;
        requireFailure(
            [&]
            {
                SceneTree::parse(invalidScene.dump());
            },
            "Persisted camera accepted a negative near plane");
        requireFailure(
            [&]
            {
                camera.setSettings({0, 0.1f, 10});
            },
            "Invalid camera FOV was accepted");
        requireFailure(
            [&]
            {
                camera.setSettings({1, 2, 1});
            },
            "Inverted camera clip range was accepted");
        requireFailure(
            [&]
            {
                tree.setCurrentCamera(group.id());
            },
            "Non-camera node became current camera");
        camera.setLocalTransform(glm::scale(glm::mat4(1), {0, 0, 0}));
        requireFailure(
            [&]
            {
                state.update(tree, {100, 50});
            },
            "Singular camera axes were accepted");
        requireFailure(
            [&]
            {
                state.update(tree, {100, 50});
            },
            "Failed camera synchronization consumed its revision");
        camera.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 3}));
        require(state.update(tree, {100, 50}) && state.camera.has_value(),
                "Corrected camera transform did not recover synchronization");
        auto removedRig = tree.remove(group);
        require(tree.currentCamera().value == 0, "Removing an ancestor left a current camera handle");
        auto& light = static_cast<LightNode&>(
            tree.addChild(tree.root(), std::make_unique<LightNode>("Light", RenderLightKind::eSpot)));
        require(tree.usesSceneLighting(), "Adding a light did not enable authored scene lighting");
        auto invalid      = light.settings();
        invalid.outerCone = invalid.innerCone;
        requireFailure(
            [&]
            {
                light.setSettings(invalid);
            },
            "Degenerate spotlight cone was accepted");
        light.setLocalTransform(glm::rotate(glm::mat4(1), 1.57079633f, {0, 1, 0}));
        state.update(tree, {100, 50});
        require(state.lights.size() == 1 &&
                    glm::length(state.lights.front().directionToLight - glm::vec3(1, 0, 0)) < 0.0001f,
                "Light direction did not follow the node's local +Z axis");
        invalidScene                   = nlohmann::json::parse(tree.serialize());
        invalidScene["scene_lighting"] = false;
        requireFailure(
            [&]
            {
                SceneTree::parse(invalidScene.dump());
            },
            "Scene lighting selection silently ignored authored lights");
        tree.remove(light);
        auto reopened = SceneTree::parse(tree.serialize());
        state.update(reopened, {100, 50});
        require(state.lighting().has_value() && state.lights.empty(),
                "Removing/saving the last light restored default sunlight");
        for (uint32_t i = 0; i <= kMaxRenderLights; ++i)
        {
            reopened.addChild(reopened.root(), std::make_unique<LightNode>("Light", RenderLightKind::ePoint));
        }
        requireFailure(
            [&]
            {
                state.update(reopened, {100, 50});
            },
            "Renderer light capacity was silently truncated");
    }
} // namespace

int main()
try
{
    namespace fs = std::filesystem;
    testRenderNodes();
    testSceneNotifications();
    testEnvironmentNodes();
    const auto root =
        fs::path("build/.tmp/scene") / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    const auto projectFile = root / "project.json";
    const auto sceneFile   = root / "main.vscene";

    vultra::ProjectManifest project;
    project.mainScene  = "main.vscene";
    const auto model   = project.addAsset("models/helmet.glb");
    project.uiDocument = project.addAsset("ui/hud.rml");
    project.uiFont     = project.addAsset("ui/hud.ttf");
    project.extensions.push_back("scripts/editor_tools.so");
    project.scripts.push_back({vultra::ScriptModule::Language::eNative, "scripts/game.so", "Game.Script"});
    project.save(projectFile);
    const auto savedProject = readText(projectFile);

    vultra::SceneTree scene(std::make_unique<vultra::Node>("World"));
    auto&      modelNode    = scene.addChild(scene.root(), std::make_unique<vultra::MeshInstanceNode>("Helmet", model));
    const auto stableNodeId = modelNode.idInScene();
    const auto runtimeNodeId = modelNode.id();
    modelNode.setLocalTransform(glm::translate(glm::mat4(1), {2, 3, 4}));
    scene.validateAssets(project);
    scene.save(sceneFile);
    const auto savedScene = readText(sceneFile);

    project.renameAsset(model, "models/renamed.glb");
    project.save(projectFile);
    auto restoredProject = vultra::ProjectManifest::load(projectFile);
    auto restoredScene   = vultra::SceneTree::load(sceneFile);
    require(restoredProject.asset(model).path == "models/renamed.glb", "Asset rename lost its stable ID");
    require(restoredProject.extensions.size() == 1 && restoredProject.extensions.front() == "scripts/editor_tools.so" &&
                restoredProject.scripts.size() == 1 && restoredProject.scripts.front().typeName == "Game.Script",
            "Native extension class declaration was lost");
    require(restoredProject.uiDocument == project.uiDocument && restoredProject.uiFont == project.uiFont,
            "UI asset declarations were lost");
    restoredScene.validateAssets(restoredProject);
    auto* restored = restoredScene.find(stableNodeId);
    require(restored && restored->id() != runtimeNodeId, "Persisted node reused a runtime ObjectId");
    require(restored->parent() == &restoredScene.root(), "Scene parent was not restored");
    require(restored->globalTransform()[3][0] == 2 && restored->globalTransform()[3][1] == 3,
            "Scene transform was not restored");
    require(restoredScene.serialize() == savedScene, "Scene serialization is not deterministic");
    requireFailure(
        [&]
        {
            restoredScene.addChild(restoredScene.root(), std::make_unique<vultra::Node>("Duplicate", stableNodeId));
        },
        "Duplicate scene node ID was accepted");

    auto detached = restoredScene.remove(*restored);
    require(!restoredScene.find(stableNodeId) && !detached->parent(), "Removed scene node remained attached");
    require(restoredScene.addChild(restoredScene.root(), std::move(detached)).idInScene() == stableNodeId,
            "Reparenting changed the stable node ID");

    auto invalidScene = savedScene;
    invalidScene.replace(invalidScene.find("MeshInstance"), 12, "UnknownThing");
    requireFailure(
        [&]
        {
            vultra::SceneTree::parse(invalidScene);
        },
        "Unknown scene node type was accepted");
    auto invalidProject      = restoredProject;
    invalidProject.mainScene = "../escape.vscene";
    requireFailure(
        [&]
        {
            invalidProject.save(projectFile);
        },
        "Project path traversal was accepted");
    auto duplicateExtensions = restoredProject;
    duplicateExtensions.extensions.push_back(duplicateExtensions.extensions.front());
    requireFailure(
        [&]
        {
            duplicateExtensions.save(projectFile);
        },
        "Duplicate extension paths were accepted");
    testSceneImport(root);
    testMaterialResources(root);
    require(readText(projectFile) != savedProject, "Project rename was not published");
    require(vultra::ProjectManifest::load(projectFile).asset(model).path == "models/renamed.glb",
            "Rejected save damaged the prior manifest");
    requireFailure(
        [&]
        {
            restoredProject.addAsset("models/../escape.glb");
        },
        "Normalized project path traversal was accepted");
    const vultra::AssetId missing {vultra::StableId::generate()};
    auto invalidTree = vultra::SceneTree(std::make_unique<vultra::MeshInstanceNode>("Missing", missing));
    requireFailure(
        [&]
        {
            invalidTree.validateAssets(restoredProject);
        },
        "Missing model asset was accepted");
    return 0;
}
catch (const std::exception& error)
{
    fprintf(stderr, "Scene test: %s\n", error.what());
    return 1;
}
