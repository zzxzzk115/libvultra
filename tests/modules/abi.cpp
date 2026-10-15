#ifdef TEST_API_SCENE
#include <vultra/api/scene_bridge.hpp>
#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#endif
#ifdef TEST_API_UI
#include <vultra/api/ui_bridge.hpp>
#include <vultra/ui/editor_gui.hpp>
#endif

#include <iostream>
#ifdef TEST_API_SCENE
#include <memory>
#endif
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
#ifdef TEST_API_SCENE
    void sceneDomain()
    {
        vultra::SceneTree      scene(std::make_unique<vultra::Node3D>("Root"));
        auto&                  child = scene.addChild(scene.root(), std::make_unique<vultra::Node3D>("Child"));
        const auto&            api   = vultra::sceneApi();
        vultra::SceneAccess    access {&scene, 1, true};
        const VultraSceneFrame frame {&access, 1};
        uint64_t               root = 0;
        require(api.version == VULTRA_ABI_VERSION && api.struct_size == sizeof(VultraSceneApi),
                "Scene domain has an invalid ABI layout");
        require(api.root_id(frame, &root) == VULTRA_STATUS_OK && root == scene.root().id().value,
                "CPU ABI did not resolve its root");
        require(api.set_node_translation(frame, child.id().value, {2, 3, 4}) == VULTRA_STATUS_OK &&
                    child.localTransform()[3] == glm::vec4(2, 3, 4, 1),
                "CPU ABI did not modify the authoritative transform");
        uint64_t          count = 0;
        vultra::SceneTree other(std::make_unique<vultra::Node>("Other"));
        require(api.child_count(frame, other.root().id().value, &count) == VULTRA_STATUS_INVALID_ARGUMENT,
                "Scene domain accepted a foreign object identity");
        access.active = false;
        require(api.root_id(frame, &root) == VULTRA_STATUS_INVALID_FRAME,
                "Scene domain accepted an expired callback borrow");
        access.active = true;
        ++access.serial;
        require(api.root_id(frame, &root) == VULTRA_STATUS_INVALID_FRAME, "Scene domain accepted a stale frame serial");
        std::cout << "CPU scene ABI passed: identity, mutation, foreign context and frame lifetime\n";
    }
#endif
#ifdef TEST_API_UI
    void uiDomain()
    {
        vultra::Device    device;
        vultra::EditorGui gui(device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false});
        const auto&       api = vultra::uiApi();
        require(api.version == VULTRA_ABI_VERSION && api.struct_size == sizeof(VultraUiApi),
                "UI domain has an invalid ABI layout");
        require(api.text({}, "text", 4) == VULTRA_STATUS_INVALID_ARGUMENT, "UI domain accepted a null frame");
        gui.begin({320, 240}, 1.0f / 60);
        const auto frame = vultra::makeUiFrame(gui);
        ImGui::Begin("ABI domain");
        require(api.text(frame, "Scene-free UI", 13) == VULTRA_STATUS_OK, "Independent UI domain could not draw");
        ImGui::End();
        gui.upload({320, 240});
        require(api.text(frame, "expired", 7) == VULTRA_STATUS_INVALID_FRAME,
                "UI domain accepted a frame after upload");
        std::cout << "UI ABI passed: independent frame, drawing and lifetime\n";
    }
#endif
} // namespace

int main()
try
{
#ifdef TEST_API_SCENE
    sceneDomain();
#endif
#ifdef TEST_API_UI
    uiDomain();
#endif
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
