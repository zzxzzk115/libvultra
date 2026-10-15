#include <vultra/api/plugin_session.hpp>
#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    struct Probe
    {
        int                   updates = 0;
        int                   stops   = 0;
        VultraSceneFrame      frame {};
        const VultraSceneApi* scene = nullptr;
    };

    struct PluginState
    {
        Probe& probe;
    };

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    VultraStatus update(void* userData, VultraSceneFrame frame, float)
    {
        auto&      probe  = static_cast<PluginState*>(userData)->probe;
        uint64_t   root   = 0;
        const auto status = probe.scene->root_id(frame, &root);
        if (status != VULTRA_STATUS_OK)
        {
            return status;
        }
        ++probe.updates;
        probe.frame = frame;
        return probe.scene->set_node_translation(frame, root, {float(probe.updates), 0, 0});
    }

    VultraStatus stop(void* userData)
    {
        std::unique_ptr<PluginState> state(static_cast<PluginState*>(userData));
        ++state->probe.stops;
        return VULTRA_STATUS_OK;
    }

    VultraStatus initialize(const VultraHostApi* host, VultraPluginApi* plugin)
    {
#ifdef TEST_API_UI
        const bool hasUi = host && host->ui;
#else
        const bool hasUi = host && !host->ui;
#endif
        if (!hasUi || !host->scene || host->version != VULTRA_ABI_VERSION ||
            host->struct_size != sizeof(VultraHostApi) || !plugin || !plugin->user_data)
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
        auto& probe       = *static_cast<Probe*>(plugin->user_data);
        probe.scene       = host->scene;
        auto state        = std::make_unique<PluginState>(probe);
        plugin->user_data = state.release();
        plugin->update    = &update;
        plugin->stop      = &stop;
        plugin->on_gui    = nullptr;
        return VULTRA_STATUS_OK;
    }
} // namespace

int main()
try
{
    vultra::SceneTree     scene(std::make_unique<vultra::Node3D>("CPU plugin"));
    Probe                 probe;
    vultra::PluginSession session(&initialize, &scene, &probe);
    session.update(1.0f / 60);
    require(probe.updates == 1 && static_cast<const vultra::Node3D&>(scene.root()).localTransform()[3].x == 1,
            "CPU plugin did not update its world through the generated ABI");
    uint64_t root = 0;
    require(probe.scene->root_id(probe.frame, &root) == VULTRA_STATUS_INVALID_FRAME,
            "Plugin callback borrow remained live after update");
    session.stop();
    session.stop();
    require(!session.active() && probe.stops == 1, "Plugin was stopped more than once");
    bool rejected = false;
    try
    {
        session.update(1.0f / 60);
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    require(rejected, "Stopped plugin still executed callbacks");
    require(probe.scene->root_id(probe.frame, &root) == VULTRA_STATUS_INVALID_FRAME,
            "Stopped plugin reactivated a callback borrow");
    std::cout << "Plugin session passed: optional UI, CPU mutation, frame lifetime and stop\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
