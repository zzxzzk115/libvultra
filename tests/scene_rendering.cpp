#include <vultra/api/scene_bridge.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/scene/scene_render_state.hpp>
#include <vultra/scripting/script_host.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <glm/ext/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace
{
    using namespace vultra;

    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    template<typename Callback>
    void expectError(Callback&& callback)
    {
        try
        {
            callback();
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        throw std::runtime_error("Invalid scene rendering input was accepted");
    }

    SceneData quad()
    {
        SceneData data;
        data.vertices = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}},
                         {{1, -1, 0}, {0, 0, 1}, {1, 0}},
                         {{1, 1, 0}, {0, 0, 1}, {1, 1}},
                         {{-1, 1, 0}, {0, 0, 1}, {0, 1}}};
        data.indices  = {0, 1, 2, 0, 2, 3};
        data.materials.emplace_back();
        data.materials.front().specularRoughness = 0.7f;
        data.primitives                          = {{0, 6, 0}};
        data.radius                              = std::sqrt(2.0f);
        return data;
    }

    float center(const Image& image)
    {
        return image.rgba[(size_t(image.size.height / 2) * image.size.width + image.size.width / 2) * 4];
    }

    bool unlitCenter(const Image& image)
    {
        for (const auto value : image.rgba)
        {
            require(std::isfinite(value), "Non-finite scene lighting output");
        }
        const auto pixel = (size_t(image.size.height / 2) * image.size.width + image.size.width / 2) * 4;
        // The renderer keeps its background color when the skybox is disabled; test the quad instead.
        return image.rgba[pixel] < 0.00001f && image.rgba[pixel + 1] < 0.00001f && image.rgba[pixel + 2] < 0.00001f;
    }

    void abiBoundaries(SceneTree& scene, ObjectId light, ObjectId camera)
    {
        const auto&            api = sceneApi();
        SceneAccess            access {&scene, 19, true};
        const VultraSceneFrame frame {&access, 19};
        VultraLightSettings    settings {};
        require(api.light_settings(frame, light.value, &settings) == VULTRA_STATUS_OK,
                "Generated light ABI cannot read its value type");
        const auto previous = settings;
        settings.intensity  = -1;
        require(api.set_light_settings(frame, light.value, settings) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.light_settings(frame, light.value, &settings) == VULTRA_STATUS_OK &&
                    settings.intensity == previous.intensity,
                "Rejected ABI light edit changed active state");
        VultraCameraSettings projection {};
        require(api.camera_settings(frame, camera.value, &projection) == VULTRA_STATUS_OK,
                "Generated camera ABI cannot read its value type");
        projection.nearPlane = projection.farPlane;
        require(api.set_camera_settings(frame, camera.value, projection) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.camera_settings(frame, light.value, &projection) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.light_settings(frame, camera.value, &settings) == VULTRA_STATUS_INVALID_ARGUMENT,
                "Generated ABI accepted invalid projection or object type");
        SceneTree foreign(std::make_unique<Node>("Foreign"));
        require(api.light_settings(frame, foreign.root().id().value, &settings) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.light_settings({&access, 18}, light.value, &settings) == VULTRA_STATUS_INVALID_FRAME,
                "Generated ABI accepted a foreign object or stale callback frame");
        auto removed = scene.remove(*scene.find(light));
        require(api.light_settings(frame, light.value, &settings) == VULTRA_STATUS_INVALID_ARGUMENT,
                "Removed light's ObjectId remained valid");
        scene.addChild(scene.root(), std::move(removed));
        access.active = false;
        require(api.light_settings(frame, light.value, &settings) == VULTRA_STATUS_INVALID_FRAME,
                "Stopped callback retained scene access");
    }

    void environmentBoundaries(SceneTree& scene, ObjectId environment, const ProjectManifest& project, AssetId hdr)
    {
        const auto&               api = sceneApi();
        SceneAccess               access {&scene, 37, true, &project};
        const VultraSceneFrame    frame {&access, 37};
        VultraEnvironmentSettings settings {};
        require(api.environment_settings(frame, environment.value, &settings) == VULTRA_STATUS_OK,
                "Generated environment ABI could not read settings");
        const auto intensity = settings.intensity;
        require(api.set_environment_settings(frame, environment.value, {-1}) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.environment_settings(frame, environment.value, &settings) == VULTRA_STATUS_OK &&
                    settings.intensity == intensity,
                "Rejected environment ABI edit changed active state");
        const auto text = hdr.value.toString();
        require(api.set_environment_asset(frame, environment.value, text.data(), text.size()) == VULTRA_STATUS_OK,
                "Generated environment ABI could not select a project HDR asset");
        const auto bad = StableId::generate().toString();
        require(api.set_environment_asset(frame, environment.value, bad.data(), bad.size()) ==
                        VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.environment_settings(frame, scene.root().id().value, &settings) ==
                        VULTRA_STATUS_INVALID_ARGUMENT,
                "Environment ABI accepted an unknown asset or wrong node type");
        SceneTree foreign(std::make_unique<Node>("Foreign"));
        require(api.set_current_environment(frame, foreign.root().id().value) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.environment_settings({&access, 36}, environment.value, &settings) ==
                        VULTRA_STATUS_INVALID_FRAME,
                "Environment ABI accepted a foreign object or stale frame");
        auto removed = scene.remove(*scene.find(environment));
        require(api.environment_settings(frame, environment.value, &settings) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.remove_node(frame, environment.value) == VULTRA_STATUS_INVALID_ARGUMENT,
                "Removed environment retained a live ABI handle");
        scene.addChild(scene.root(), std::move(removed));
        scene.setCurrentEnvironment(environment);
        access.active = false;
        require(api.environment_settings(frame, environment.value, &settings) == VULTRA_STATUS_INVALID_FRAME,
                "Stopped callback retained environment access");
    }

    void materialBoundaries(SceneTree& scene, MeshInstanceNode& mesh, MaterialResource& material)
    {
        const auto&              api = sceneApi();
        SceneAccess              access {&scene, 31, true};
        const VultraSceneFrame   frame {&access, 31};
        VultraMaterialParameters value {};
        require(api.material_parameters(frame, material.id().value, &value) == VULTRA_STATUS_OK,
                "Generated material ABI cannot read the resource");
        const auto original     = material.parameters();
        value.specularRoughness = -1;
        require(api.set_material_parameters(frame, material.id().value, value) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    material.parameters() == original,
                "Rejected ABI material edit changed active state");
        SceneTree foreign(std::make_unique<Node>("Foreign"));
        auto&     other = foreign.addMaterial(std::make_unique<MaterialResource>("Foreign material"));
        require(api.material_parameters(frame, mesh.id().value, &value) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.set_mesh_material(frame, mesh.id().value, 0, other.id().value) ==
                        VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.material_parameters({&access, 30}, material.id().value, &value) == VULTRA_STATUS_INVALID_FRAME,
                "Material ABI accepted a wrong type, foreign resource or stale frame");
        uint64_t id = 0;
        require(api.mesh_material(frame, mesh.id().value, 0, &id) == VULTRA_STATUS_OK && id == material.id().value,
                "Material ABI lost the mesh resource reference");
        require(api.remove_material(frame, id) == VULTRA_STATUS_OK &&
                    api.remove_material(frame, id) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.material_parameters(frame, id, &value) == VULTRA_STATUS_INVALID_ARGUMENT &&
                    api.mesh_material(frame, mesh.id().value, 0, &id) == VULTRA_STATUS_OK && id == 0,
                "Removing a material did not invalidate its handle and clear mesh overrides");
    }
} // namespace

int main(int argc, char** argv)
try
{
    using namespace vultra;
    require(argc > 0, "Missing executable path");
    const auto directory = std::filesystem::path("build/.tmp") / ("scene-lights-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    const auto    hdrFile = directory / "constant.hdr";
    std::ofstream hdrOutput(hdrFile, std::ios::binary);
    hdrOutput << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    const unsigned char hdrPixel[4] {128, 64, 32, 129};
    for (uint32_t i = 0; i < 8; ++i)
    {
        hdrOutput.write(reinterpret_cast<const char*>(hdrPixel), 4);
    }
    hdrOutput.close();
    ProjectManifest environmentProject;
    const auto      hdrAsset = environmentProject.addAsset("constant.hdr");
    Device          device;
    RenderingServer server(device);
    auto            gpu      = server.uploadScene(quad());
    const auto      rid      = gpu.rid();
    const auto*     vertices = gpu->vertices->handle;
    Environment     environment(device);
    BuiltinRenderer renderer(device, *gpu, environment);
    renderer.settings.ibl              = false;
    renderer.settings.skybox           = false;
    renderer.settings.shadowFilter     = ShadowFilter::eDisabled;
    renderer.settings.directionToLight = {0, 0, 1};
    renderer.settings.lightColor       = {1, 1, 1};
    renderer.settings.lightIntensity   = 2;
    Frame                frame(device);
    constexpr Extent     extent {65, 65};
    std::array<Image, 2> scriptReferences;
    for (const auto path : {RenderPath::eNaiveForward, RenderPath::eNaiveDeferred})
    {
        renderer.settings.ibl    = false;
        renderer.settings.skybox = false;
        renderer.settings.path   = path;
        RenderGraph graph(device);
        const auto  outputs = renderer.addPasses(graph, extent);
        graph.exportResource(outputs.hdr);
        graph.exportResource(outputs.color);
        graph.compile();
        const auto*   hdrTexture = graph.getTexture(outputs.hdr).handle;
        SceneTree     tree(std::make_unique<Node>("World"));
        const AssetId model {StableId::generate()};
        auto&         mesh = static_cast<MeshInstanceNode&>(
            tree.addChild(tree.root(), std::make_unique<MeshInstanceNode>("Quad", model)));
        std::vector<SceneMeshInstance> instances {{mesh.idInScene(),
                                                   model,
                                                   glm::mat4(1),
                                                   glm::mat4(1),
                                                   0,
                                                   1,
                                                   0,
                                                   {MaterialParameters::fromMaterial(quad().materials.front())}}};
        auto& camera = static_cast<CameraNode&>(tree.addChild(tree.root(), std::make_unique<CameraNode>("Camera")));
        camera.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 3}));
        tree.setCurrentCamera(camera.id());
        SceneRenderState state;
        SceneGpuSync     gpuSync;
        const auto       render = [&]
        {
            if (gpuSync.environmentChanged(tree))
            {
                environment.setSource(sceneEnvironmentPath(tree, environmentProject, directory));
            }
            gpuSync.update(tree, instances, *gpu);
            state.update(tree, extent);
            require(state.camera.has_value(), "Current scene camera was not selected");
            renderer.prepare(*state.camera, graph, outputs, state.lighting(), state.environmentIntensity);
            graph.execute(frame.begin());
            frame.submitAndWait();
            require(center(readback(device, graph.getTexture(outputs.depth))) < 1,
                    "The lighting probe missed the test geometry");
            return readback(device, graph.getTexture(outputs.hdr));
        };
        const auto preset = render();
        mesh.setModel({StableId::generate()});
        require(gpuSync.needsImport(tree, instances), "Model replacement did not request geometry reimport");
        expectError(
            [&]
            {
                gpuSync.update(tree, instances, *gpu);
            });
        mesh.setModel(model);
        require(!gpuSync.needsImport(tree, instances) && render().rgba == preset.rgba,
                "Restoring the model after rejected synchronization did not recover the image");
        auto detachedMesh = tree.remove(mesh);
        require(gpuSync.needsImport(tree, instances), "Removing a mesh retained stale geometry");
        expectError(
            [&]
            {
                gpuSync.update(tree, instances, *gpu);
            });
        tree.addChild(tree.root(), std::move(detachedMesh));
        require(!gpuSync.needsImport(tree, instances) && render().rgba == preset.rgba,
                "Reattaching the same mesh did not recover synchronization");
        auto& addedMesh = tree.addChild(tree.root(), std::make_unique<MeshInstanceNode>("Added quad", model));
        require(gpuSync.needsImport(tree, instances), "A new mesh was not detected before recording");
        expectError(
            [&]
            {
                gpuSync.update(tree, instances, *gpu);
            });
        tree.remove(addedMesh);
        require(!gpuSync.needsImport(tree, instances) && render().rgba == preset.rgba,
                "Removing an unimported mesh did not recover synchronization");
        auto         parallelGpu       = server.uploadScene(quad());
        auto         parallelInstances = instances;
        SceneGpuSync parallelSync;
        parallelSync.update(tree, parallelInstances, *parallelGpu);
        BuiltinRenderer parallelRenderer(device, *parallelGpu, environment);
        parallelRenderer.settings = renderer.settings;
        RenderGraph parallelGraph(device);
        const auto  parallelOutputs = parallelRenderer.addPasses(parallelGraph, extent);
        parallelGraph.exportResource(parallelOutputs.hdr);
        parallelGraph.compile();
        require(!gpuSync.update(tree, instances, *gpu) && !state.update(tree, extent),
                "Unchanged frame repeated scene synchronization");
        auto& rig = tree.addChild(tree.root(), std::make_unique<Node>("Mesh rig"));
        rig.setLocalTransform(glm::translate(glm::mat4(1), {0.6f, 0, 0}));
        tree.reparent(mesh, rig);
        require(!gpuSync.needsImport(tree, instances) && render().rgba != preset.rgba && gpu.rid() == rid &&
                    gpu->vertices->handle == vertices && graph.getTexture(outputs.hdr).handle == hdrTexture,
                "Reparented mesh or inherited motion rebuilt geometry or failed to render");
        tree.reparent(mesh, tree.root());
        tree.remove(rig);
        require(!gpuSync.needsImport(tree, instances) && render().rgba == preset.rgba,
                "Restoring mesh parent changed its original image or required reimport");
        auto& sun = static_cast<LightNode&>(
            tree.addChild(tree.root(), std::make_unique<LightNode>("Sun", RenderLightKind::eDirectional)));
        auto sunSettings      = sun.settings();
        sunSettings.intensity = 2;
        sun.setSettings(sunSettings);
        require(render().rgba == preset.rgba && center(preset) > 0.1f, "Scene sun differs from direct C++ lighting");
        auto& material            = tree.addMaterial(std::make_unique<MaterialResource>("Live material"));
        auto  parameters          = instances.front().importedMaterials.front();
        parameters.baseRed        = 0.25f;
        parameters.specularWeight = 0;
        material.setParameters(parameters);
        mesh.setMaterial(0, material.assetId());
        const auto tinted = render();
        require(parallelSync.update(tree, parallelInstances, *parallelGpu),
                "The first GPU consumer consumed another consumer's material notification");
        parallelRenderer.prepare(*state.camera,
                                 parallelGraph,
                                 parallelOutputs,
                                 state.lighting(),
                                 state.environmentIntensity);
        parallelGraph.execute(frame.begin());
        frame.submitAndWait();
        require(readback(device, parallelGraph.getTexture(parallelOutputs.hdr)).rgba == tinted.rgba,
                "Independent GPU consumers produced different frames from the same material change");
        require(center(tinted) < center(preset) * 0.4f, "Live material color did not change the next HDR frame");
        tree.save(directory /
                  (std::string(path == RenderPath::eNaiveForward ? "forward" : "deferred") + "-material.vscene"));
        auto materialCopy = SceneTree::load(
            directory / (std::string(path == RenderPath::eNaiveForward ? "forward" : "deferred") + "-material.vscene"));
        require(materialCopy.serialize() == tree.serialize() &&
                    materialCopy.findMaterial(material.assetId())->parameters() == parameters,
                "Shared material persistence changed parameters or asset IDs");
        mesh.setMaterial(1, material.assetId());
        const auto retained = gpu->materials.front();
        expectError(
            [&]
            {
                gpuSync.update(tree, instances, *gpu);
            });
        require(MaterialParameters::fromMaterial(gpu->materials.front()) == MaterialParameters::fromMaterial(retained),
                "Invalid material slot changed GPU parameters before failure");
        mesh.setMaterial(1, {});
        materialBoundaries(tree, mesh, material);
        require(render().rgba == preset.rgba, "Clearing an override did not restore the imported material");
        sunSettings.intensity = 4;
        sun.setSettings(sunSettings);
        require(std::abs(center(render()) - center(preset) * 2) < 0.002f, "Live directional intensity is not linear");
        sunSettings.intensity = 0;
        sun.setSettings(sunSettings);
        require(unlitCenter(render()), "Disabled direct lighting retained the renderer preset");

        auto& point = static_cast<LightNode&>(
            tree.addChild(tree.root(), std::make_unique<LightNode>("Point", RenderLightKind::ePoint)));
        point.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 2}));
        auto pointSettings      = point.settings();
        pointSettings.intensity = 10;
        pointSettings.range     = 100;
        point.setSettings(pointSettings);
        const auto nearPoint = render();
        point.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 4}));
        require(std::abs(center(render()) * 4 - center(nearPoint)) < 0.002f,
                "Point light does not follow inverse-square falloff");
        point.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 2}));
        pointSettings.range = 1;
        point.setSettings(pointSettings);
        require(unlitCenter(render()), "Point light exceeded its finite range");
        pointSettings.intensity = 0;
        point.setSettings(pointSettings);

        auto& spot = static_cast<LightNode&>(
            tree.addChild(tree.root(), std::make_unique<LightNode>("Spot", RenderLightKind::eSpot)));
        spot.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 2}));
        auto spotSettings      = spot.settings();
        spotSettings.intensity = 10;
        spotSettings.range     = 100;
        spotSettings.innerCone = 0.1f;
        spotSettings.outerCone = 0.2f;
        spot.setSettings(spotSettings);
        const auto spotImage = render();
        require(std::abs(center(spotImage) - center(nearPoint)) < 0.002f && spotImage.rgba != nearPoint.rgba,
                "Spot cone did not preserve axis intensity and restrict off-axis lighting");
        const auto prefix = path == RenderPath::eNaiveForward ? "forward" : "deferred";
        savePng(readback(device, graph.getTexture(outputs.color)), directory / (std::string(prefix) + "-spot.png"));
        spot.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 2}) *
                               glm::rotate(glm::mat4(1), std::numbers::pi_v<float>, {0, 1, 0}));
        require(unlitCenter(render()), "Spot direction ignored its node rotation");
        spot.setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 2}));
        const auto stable = render();
        camera.setLocalTransform(glm::translate(glm::mat4(1), {0.5f, 0, 3}));
        require(render().rgba != stable.rgba, "Moving the scene camera did not change rendering");
        abiBoundaries(tree, point.id(), camera.id());
        tree.save(directory / (std::string(prefix) + ".vscene"));
        const auto saved    = render();
        auto       reopened = SceneTree::load(directory / (std::string(prefix) + ".vscene"));
        require(reopened.serialize() == tree.serialize(), "Camera/light persistence is not deterministic");
        tree = std::move(reopened);
        require(render().rgba == saved.rgba, "Saved camera/lights changed the rendered output");
        require(gpu.rid() == rid && gpu->vertices->handle == vertices &&
                    graph.getTexture(outputs.hdr).handle == hdrTexture,
                "Camera/light edits reuploaded geometry or rebuilt graph textures");

        renderer.settings.ibl    = true;
        renderer.settings.skybox = true;
        auto& worldEnvironment   = static_cast<EnvironmentNode&>(
            tree.addChild(tree.root(), std::make_unique<EnvironmentNode>("Live environment")));
        tree.setCurrentEnvironment(worldEnvironment.id());
        const auto* radiance = environment.radiance->handle;
        worldEnvironment.setSettings({0.5f});
        const auto dim = render();
        worldEnvironment.setSettings({1});
        const auto bright = render();
        require(center(dim) < center(bright) && std::abs(dim.rgba[0] * 2 - bright.rgba[0]) < 0.004f &&
                    environment.radiance->handle == radiance,
                "Authored environment intensity did not affect skybox/IBL without replacing textures");
        environmentBoundaries(tree, worldEnvironment.id(), environmentProject, hdrAsset);
        const auto mapped = render();
        require(mapped.rgba != bright.rgba && environment.source() == hdrFile,
                "Authored HDR asset did not change rendering through the existing graph");
        const auto* mappedRadiance = environment.radiance->handle;
        environment.setSource(hdrFile);
        require(environment.radiance->handle == mappedRadiance, "Unchanged environment source recreated GPU textures");
        bool rejected = false;
        try
        {
            environment.setSource(directory / "missing.hdr");
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        require(rejected && environment.source() == hdrFile && environment.radiance->handle == mappedRadiance &&
                    render().rgba == mapped.rgba && gpu.rid() == rid && gpu->vertices->handle == vertices &&
                    graph.getTexture(outputs.hdr).handle == hdrTexture,
                "Failed environment replacement damaged textures, geometry or the active graph");
        tree.save(directory / (std::string(prefix) + "-environment.vscene"));
        tree = SceneTree::load(directory / (std::string(prefix) + "-environment.vscene"));
        require(render().rgba == mapped.rgba, "Persisted environment changed HDR rendering");
        savePng(readback(device, graph.getTexture(outputs.color)),
                directory / (std::string(prefix) + "-environment.png"));

#ifdef _WIN32
        const auto native = std::filesystem::absolute(argv[0]).parent_path() / "test-native-scene-script.dll";
#else
        const auto native = std::filesystem::absolute(argv[0]).parent_path() / "libtest-native-scene-script.so";
#endif
        const std::array modules {ScriptModule {ScriptModule::Language::eNative, native, "LightingController"},
                                  ScriptModule {ScriptModule::Language::eLua, "tests/scene_scripts/lighting.lua"},
                                  ScriptModule {ScriptModule::Language::eCSharp,
                                                "build/.tmp/scripting-managed/Vultra.SceneScripts.dll",
                                                "VultraTests.LightingController"}};
        for (size_t i = 0; i < modules.size(); ++i)
        {
            tree                   = SceneTree(std::make_unique<Node>("Script world"));
            auto& scriptMesh       = tree.addChild(tree.root(), std::make_unique<MeshInstanceNode>("Quad", model));
            instances.front().node = scriptMesh.idInScene();
            auto module            = modules[i];
            module.node            = tree.root().idInScene().value;
            ScriptHost host(tree, false);
            host.add(module, std::filesystem::absolute(module.path));
            host.update(0.25f);
            const auto first = render();
            require(!unlitCenter(first) && state.lights.size() == 1 && state.lights.front().intensity == 2.25f,
                    "Script did not create the same scene camera/light through the common ABI");
            require(tree.materials().size() == 1 && gpu->materials.front().baseColor.g == 0.25f &&
                        gpu->materials.front().baseColor.r == 0.65f,
                    "Script did not update shared material parameters through the common ABI");
            require(tree.currentEnvironment().value != 0 && state.environmentIntensity == 1.25f,
                    "Script did not create/select/update the common environment object");
            host.update(0.5f);
            require(render().rgba != first.rgba && state.lights.front().intensity == 2.5f,
                    "Script light update did not change the next completed frame");
            require(state.environmentIntensity == 1.5f, "Script environment update missed the next completed frame");
            if (i == 0)
            {
                scriptReferences[size_t(path)] = first;
            }
            else
            {
                require(first.rgba == scriptReferences[size_t(path)].rgba,
                        "Native, Lua and safe C# scene rendering outputs differ");
            }
            const auto savedScene = directory / (std::string(prefix) + "-script-" + std::to_string(i) + ".vscene");
            tree.save(savedScene);
            host.stop();
            const auto afterUpdate = render();
            tree                   = SceneTree::load(savedScene);
            require(render().rgba == afterUpdate.rgba, "Script-authored scene save/reload changed rendering");
        }
    }
    std::cout
        << "Scene GPU tests passed: both paths, independent notifications, reparenting, topology rejection/retry, "
           "camera/light/material/environment updates, resource reuse, persistence and native/Lua/C# parity; "
           "captures "
        << directory << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
