#include <vultra/api/native_plugin.hpp>
#include <vultra/api/scene_bridge.hpp>
#include <vultra/api/ui_bridge.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/native_script.hpp>

#include <array>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    class ProbeScript final : public vultra::scripting::NativeScript
    {};

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void checkNativeScriptLayouts()
    {
        using namespace vultra;
        using namespace vultra::scripting;
        const std::array classes {scriptClass<ProbeScript>("Probe")};
        NativeScriptInit request {1, "Probe", 5};
        auto             ui    = uiApi();
        auto             scene = sceneApi();
        VultraHostApi    host {VULTRA_ABI_VERSION, sizeof(VultraHostApi), &ui, &scene, nullptr};
        const auto       initialize = [&]
        {
            VultraPluginApi plugin {};
            plugin.version     = VULTRA_ABI_VERSION;
            plugin.struct_size = sizeof(VultraPluginApi);
            plugin.user_data   = &request;
            const auto status  = initializeNativeModule(&host, &plugin, classes);
            if (status == VULTRA_STATUS_OK)
            {
                require(plugin.stop(plugin.user_data) == VULTRA_STATUS_OK, "Native layout probe did not stop");
            }
            return status;
        };
        require(initialize() == VULTRA_STATUS_OK, "Current version-1 native SDK layout was rejected");
        for (const auto size : {sizeof(VultraSceneApi) - sizeof(void*), sizeof(VultraSceneApi) + sizeof(void*)})
        {
            scene.struct_size = uint32_t(size);
            require(initialize() == VULTRA_STATUS_INVALID_ARGUMENT, "Stale version-1 scene table layout was accepted");
        }
        scene.struct_size = sizeof(VultraSceneApi);
        ui.struct_size += sizeof(void*);
        require(initialize() == VULTRA_STATUS_INVALID_ARGUMENT, "Larger version-1 UI table was accepted");
        ui.struct_size = sizeof(VultraUiApi);
        host.struct_size += sizeof(void*);
        require(initialize() == VULTRA_STATUS_INVALID_ARGUMENT, "Larger version-1 host table was accepted");
    }
} // namespace

int main(int argc, char** argv)
{
    require(argc > 0, "Missing executable path");
    checkNativeScriptLayouts();
    const auto directory = std::filesystem::absolute(argv[0]).parent_path();
#ifdef _WIN32
    const auto good = directory / "example-native-plugin.dll";
    const auto bad  = directory / "test-native-plugin-bad.dll";
#else
    const auto good = directory / "libexample-native-plugin.so";
    const auto bad  = directory / "libtest-native-plugin-bad.so";
#endif
    const auto& ui      = vultra::uiApi();
    uint8_t     changed = 0;
    require(ui.version == VULTRA_ABI_VERSION && ui.struct_size == sizeof(VultraUiApi), "UI ABI metadata mismatch");
    require(ui.button({nullptr, 0}, "x", 1, &changed) == VULTRA_STATUS_INVALID_ARGUMENT,
            "UI ABI accepted a null frame");
    vultra::SceneTree scene(std::make_unique<vultra::Node>("Root"));
    auto&             child    = scene.addChild(scene.root(), std::make_unique<vultra::Node>("Child"));
    const auto&       sceneApi = vultra::sceneApi();
    require(sceneApi.version == VULTRA_ABI_VERSION && sceneApi.struct_size == sizeof(VultraSceneApi),
            "Scene ABI metadata mismatch");
    vultra::ProjectManifest project;
    const auto              selectableModel = project.addAsset("models/other.obj");
    const auto              imageAsset      = project.addAsset("ui/icon.png");
    vultra::SceneAccess     access {&scene, 7, true, &project};
    VultraSceneFrame        frame {&access, 7};
    uint64_t                root     = 0;
    uint64_t                count    = 0;
    uint64_t                first    = 0;
    const char*             name     = nullptr;
    uint64_t                nameSize = 0;
    require(sceneApi.root_id(frame, &root) == VULTRA_STATUS_OK && root == scene.root().id().value,
            "Scene ABI did not return the root ObjectId");
    require(sceneApi.child_count(frame, root, &count) == VULTRA_STATUS_OK && count == 1,
            "Scene ABI did not return the child count");
    require(sceneApi.child_id(frame, root, 0, &first) == VULTRA_STATUS_OK && first == child.id().value,
            "Scene ABI did not return the child ObjectId");
    require(sceneApi.node_name(frame, first, &name, &nameSize) == VULTRA_STATUS_OK &&
                std::string_view(name, nameSize) == "Child",
            "Scene ABI did not return the child name");
    VultraSceneTranslation position {};
    require(sceneApi.node_translation(frame, first, &position) == VULTRA_STATUS_OK && position.x == 0,
            "Scene ABI did not read local translation");
    require(sceneApi.set_node_translation(frame, first, {1, 2, 3}) == VULTRA_STATUS_OK &&
                sceneApi.node_translation(frame, first, &position) == VULTRA_STATUS_OK && position.x == 1 &&
                position.y == 2 && position.z == 3 && child.localTransform()[3].x == 1,
            "Scene ABI did not write local translation");
    const vultra::AssetId firstModel {vultra::StableId::generate()};
    const vultra::AssetId secondModel {vultra::StableId::generate()};
    auto&                 sourceMesh = static_cast<vultra::MeshInstanceNode&>(
        scene.addChild(scene.root(), std::make_unique<vultra::MeshInstanceNode>("Source", firstModel)));
    auto& targetMesh = static_cast<vultra::MeshInstanceNode&>(
        scene.addChild(scene.root(), std::make_unique<vultra::MeshInstanceNode>("Target", secondModel)));
    uint64_t duplicate = 0;
    require(sceneApi.duplicate_mesh(frame, sourceMesh.id().value, root, &duplicate) == VULTRA_STATUS_OK &&
                duplicate != sourceMesh.id().value && scene.find(vultra::ObjectId {duplicate}) &&
                scene.find(vultra::ObjectId {duplicate})->idInScene() != sourceMesh.idInScene(),
            "Scene ABI did not create a distinct mesh instance");
    require(sceneApi.copy_mesh_model(frame, targetMesh.id().value, sourceMesh.id().value) == VULTRA_STATUS_OK &&
                targetMesh.model() == firstModel,
            "Scene ABI did not copy the model asset reference");
    const auto selectableText = selectableModel.value.toString();
    const auto unknownText    = vultra::StableId::generate().toString();
    const auto imageText      = imageAsset.value.toString();
    require(sceneApi.set_mesh_model(frame, targetMesh.id().value, selectableText.data(), selectableText.size()) ==
                    VULTRA_STATUS_OK &&
                targetMesh.model() == selectableModel,
            "Scene ABI did not select a project model asset");
    require(sceneApi.set_mesh_model(frame, first, selectableText.data(), selectableText.size()) ==
                    VULTRA_STATUS_INVALID_ARGUMENT &&
                sceneApi.set_mesh_model(frame, targetMesh.id().value, unknownText.data(), unknownText.size()) ==
                    VULTRA_STATUS_INVALID_ARGUMENT &&
                sceneApi.set_mesh_model(frame, targetMesh.id().value, imageText.data(), imageText.size()) ==
                    VULTRA_STATUS_INVALID_ARGUMENT &&
                sceneApi.set_mesh_model(frame, targetMesh.id().value, "bad", 3) == VULTRA_STATUS_INVALID_ARGUMENT &&
                sceneApi.set_mesh_model(frame, targetMesh.id().value, nullptr, 1) == VULTRA_STATUS_INVALID_ARGUMENT &&
                targetMesh.model() == selectableModel,
            "Scene ABI changed the model after an invalid asset ID");
    uint64_t groupId = 0;
    uint64_t meshId  = 0;
    require(sceneApi.create_node(frame, root, "Group", 5, &groupId) == VULTRA_STATUS_OK &&
                scene.find(vultra::ObjectId {groupId})->name() == "Group",
            "Scene ABI did not create a group node");
    require(
        sceneApi.set_node_translation(frame, groupId, {2, 0, 0}) == VULTRA_STATUS_OK &&
            sceneApi.create_mesh(frame, root, "Spawned", 7, selectableText.data(), selectableText.size(), &meshId) ==
                VULTRA_STATUS_OK,
        "Scene ABI did not create a mesh from the project asset catalog");
    auto& createdMesh = static_cast<vultra::MeshInstanceNode&>(*scene.find(vultra::ObjectId {meshId}));
    require(createdMesh.model() == selectableModel &&
                sceneApi.set_node_translation(frame, meshId, {0, 1, 0}) == VULTRA_STATUS_OK &&
                sceneApi.reparent_node(frame, meshId, groupId) == VULTRA_STATUS_OK &&
                createdMesh.parent()->id().value == groupId && createdMesh.localTransform()[3].y == 1 &&
                createdMesh.globalTransform()[3].x == 2,
            "Scene ABI did not preserve local transform and inherit the new parent transform");
    require(sceneApi.reparent_node(frame, groupId, meshId) == VULTRA_STATUS_INVALID_ARGUMENT &&
                sceneApi.reparent_node(frame, root, groupId) == VULTRA_STATUS_INVALID_ARGUMENT &&
                createdMesh.parent()->id().value == groupId,
            "Scene ABI allowed a cycle or root reparent");
    require(sceneApi.create_node(frame, root, "", 0, &duplicate) == VULTRA_STATUS_INVALID_ARGUMENT &&
                sceneApi.create_mesh(frame, root, "Bad", 3, "bad", 3, &duplicate) == VULTRA_STATUS_INVALID_ARGUMENT &&
                scene.find(vultra::ObjectId {groupId})->children().size() == 1,
            "Scene ABI left a node behind after invalid creation");
    vultra::SceneAccess withoutProject {&scene, 7, true};
    require(sceneApi.set_mesh_model({&withoutProject, 7},
                                    targetMesh.id().value,
                                    selectableText.data(),
                                    selectableText.size()) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI selected a model without a project asset catalog");
    require(sceneApi.duplicate_mesh(frame, first, root, &duplicate) == VULTRA_STATUS_INVALID_ARGUMENT &&
                sceneApi.copy_mesh_model(frame, first, sourceMesh.id().value) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI accepted a group node as a mesh instance");
    require(sceneApi.set_node_translation(frame, first, {std::numeric_limits<float>::infinity(), 0, 0}) ==
                    VULTRA_STATUS_INVALID_ARGUMENT &&
                child.localTransform()[3].x == 1,
            "Scene ABI accepted a non-finite translation");
    require(sceneApi.child_id(frame, root, scene.root().children().size(), &first) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI accepted an out-of-range child index");
    require(sceneApi.remove_node(frame, root) == VULTRA_STATUS_INVALID_ARGUMENT, "Scene ABI removed the root node");
    require(sceneApi.remove_node(frame, duplicate) == VULTRA_STATUS_OK && !scene.find(vultra::ObjectId {duplicate}) &&
                sceneApi.remove_node(frame, duplicate) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI did not invalidate a removed mesh ObjectId");
    vultra::SceneTree   other(std::make_unique<vultra::Node>("Other"));
    vultra::SceneAccess otherAccess {&other, 7, true};
    require(sceneApi.child_count({&otherAccess, 7}, root, &count) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI accepted an ObjectId from another scene");
    require(sceneApi.duplicate_mesh({&otherAccess, 7}, sourceMesh.id().value, other.root().id().value, &duplicate) ==
                    VULTRA_STATUS_INVALID_ARGUMENT &&
                sceneApi.copy_mesh_model({&otherAccess, 7}, targetMesh.id().value, sourceMesh.id().value) ==
                    VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI edited a mesh from another scene");
    require(sceneApi.set_node_translation({&otherAccess, 7}, first, {4, 5, 6}) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI wrote a node from another scene");
    require(
        sceneApi.set_mesh_model({&access, 6}, targetMesh.id().value, selectableText.data(), selectableText.size()) ==
                VULTRA_STATUS_INVALID_FRAME &&
            sceneApi.duplicate_mesh({&access, 6}, sourceMesh.id().value, root, &duplicate) ==
                VULTRA_STATUS_INVALID_FRAME &&
            sceneApi.remove_node({&access, 6}, targetMesh.id().value) == VULTRA_STATUS_INVALID_FRAME,
        "Scene ABI duplicated a mesh through a stale frame");
    require(sceneApi.root_id({&access, 6}, &root) == VULTRA_STATUS_INVALID_FRAME,
            "Scene ABI accepted a stale callback frame");
    require(sceneApi.set_node_translation({&access, 6}, first, {4, 5, 6}) == VULTRA_STATUS_INVALID_FRAME &&
                child.localTransform()[3].x == 1,
            "Scene ABI wrote through a stale frame");
    access.active = false;
    require(sceneApi.root_id(frame, &root) == VULTRA_STATUS_INVALID_FRAME, "Scene ABI accepted a frame outside update");
    require(sceneApi.root_id({nullptr, 0}, &root) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI accepted a null context");
    vultra::NativePlugin uiOnly(good);
    uiOnly.update(0.1f);
    uiOnly.stop();
    vultra::NativePlugin plugin(good, &scene);
    plugin.update(0.5f);
    plugin.stop();
    require(!plugin.active(), "Native plugin remained active after stop");
    bool stoppedRejected = false;
    try
    {
        plugin.update(0.5f);
    }
    catch (const std::logic_error&)
    {
        stoppedRejected = true;
    }
    require(stoppedRejected, "Stopped plugin callback was accepted");
    bool versionRejected = false;
    try
    {
        vultra::NativePlugin incompatible(bad);
    }
    catch (const std::runtime_error&)
    {
        versionRejected = true;
    }
    require(versionRejected, "Incompatible plugin ABI version was accepted");
}
