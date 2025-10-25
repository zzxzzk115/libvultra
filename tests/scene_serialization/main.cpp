#include <vultra/function/io/serialization.hpp>
#include <vultra/function/scenegraph/entity.hpp>
#include <vultra/function/scenegraph/logic_scene.hpp>

using namespace vultra;

int main()
{
    LogicScene scene {"Test Scene"};

    scene.createEntity("Camera");
    scene.createEntity("Light");
    scene.createEntity("Mesh");

    // Calculate time (us) taken to save the scene
    auto start = std::chrono::high_resolution_clock::now();
    scene.saveTo("scene.vscene");
    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    VULTRA_CLIENT_INFO("Scene ({}) saved in {} us", scene.getName(), duration);

    LogicScene loadedScene {};

    // Calculate time (us) taken to load the scene
    start = std::chrono::high_resolution_clock::now();
    loadedScene.loadFrom("scene.vscene");
    end      = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    VULTRA_CLIENT_INFO("Scene ({}) loaded in {} us", loadedScene.getName(), duration);

    VULTRA_CLIENT_INFO("Entities in loaded scene:");
    for (auto& entity : loadedScene.getEntitiesSortedByName())
    {
        VULTRA_CLIENT_INFO(" - {} {}", entity.getName(), entity.getCoreUUID().toString());
    }

    return 0;
}