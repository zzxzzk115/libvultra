#include <vultra/assets/project_manifest.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

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
        auto imported                = vultra::importScene(scene, project, root, options);
        require(imported.scene.vertices.size() == 9 && imported.scene.indices.size() == 9 &&
                    imported.scene.primitives.size() == 3,
                "Scene mesh instances were not composed");
        require(imported.scene.materials.size() == 2 && imported.textures.materials.size() == 2 &&
                    imported.textures.images.size() == 4,
                "Repeated model duplicated its material or prepared texture data");
        require(imported.scene.primitives[0].material == 0 && imported.scene.primitives[1].material == 0 &&
                    imported.scene.primitives[2].material == 1,
                "Scene material offsets are incorrect");
        require(imported.scene.indices[3] == imported.scene.indices[0] + 3 &&
                    imported.scene.indices[4] == imported.scene.indices[2] + 3 &&
                    imported.scene.indices[5] == imported.scene.indices[1] + 3,
                "Reflected mesh winding was not corrected");
        require(glm::length(imported.scene.center - glm::vec3(1, 2.5f, 1.5f)) < 0.0001f &&
                    std::abs(imported.scene.radius - 0.5f * std::sqrt(14.0f)) < 0.0001f,
                "Composed scene bounds ignore node transforms");

        reflected.setLocalTransform(glm::scale(glm::mat4(1), {0, 1, 1}));
        requireFailure(
            [&]
            {
                vultra::importScene(scene, project, root, options);
            },
            "Singular mesh transform was accepted");
    }
} // namespace

int main()
try
{
    namespace fs = std::filesystem;
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
