#include <vultra/api/native_plugin.hpp>
#include <vultra/api/scene_bridge.hpp>
#include <vultra/api/ui_bridge.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

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

int main(int argc, char** argv)
{
    require(argc > 0, "Missing executable path");
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
    vultra::SceneAccess access {&scene, 7, true};
    VultraSceneFrame    frame {&access, 7};
    uint64_t            root     = 0;
    uint64_t            count    = 0;
    uint64_t            first    = 0;
    const char*         name     = nullptr;
    uint64_t            nameSize = 0;
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
    require(sceneApi.set_node_translation(frame, first, {std::numeric_limits<float>::infinity(), 0, 0}) ==
                    VULTRA_STATUS_INVALID_ARGUMENT &&
                child.localTransform()[3].x == 1,
            "Scene ABI accepted a non-finite translation");
    require(sceneApi.child_id(frame, root, 1, &first) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI accepted an out-of-range child index");
    vultra::SceneTree   other(std::make_unique<vultra::Node>("Other"));
    vultra::SceneAccess otherAccess {&other, 7, true};
    require(sceneApi.child_count({&otherAccess, 7}, root, &count) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI accepted an ObjectId from another scene");
    require(sceneApi.set_node_translation({&otherAccess, 7}, first, {4, 5, 6}) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Scene ABI wrote a node from another scene");
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
