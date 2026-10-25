#include <vultra/assets/meshlet_data.hpp>
#include <vultra/assets/scene_data.hpp>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
    SceneData scene;
    scene.vertices      = {{{-1, -1, 0}}, {{1, -1, 0}}, {{1, 1, 0}}, {{-1, 1, 0}}};
    scene.indices       = {0, 1, 2, 0, 2, 3};
    scene.primitives    = {{0, 3, 0}, {3, 3, 0}};
    const auto serial   = buildMeshlets(scene, 1);
    const auto parallel = buildMeshlets(scene, 4);
    require(serial.meshlets.size() == 2 && serial.primitives.size() == 2 && serial.triangles.size() == 2,
            "Meshlet cooking lost primitive geometry");
    require(serial.vertices == parallel.vertices && serial.triangles == parallel.triangles &&
                serial.meshlets.size() == parallel.meshlets.size() &&
                std::memcmp(serial.meshlets.data(),
                            parallel.meshlets.data(),
                            serial.meshlets.size() * sizeof(Meshlet)) == 0,
            "Parallel meshlet cooking changed its payload");
    for (uint32_t index = 0; index < scene.primitives.size(); ++index)
    {
        const auto range = serial.primitives[index];
        require(range.first == index && range.count == 1, "Meshlet primitive mapping changed");
        const auto geometry = serial.meshlets[range.first].geometry;
        const auto triangle = serial.triangles[geometry.y];
        for (uint32_t corner = 0; corner < 3; ++corner)
        {
            const auto local = (triangle >> (corner * 8)) & 255;
            require(local < geometry.z && serial.vertices[geometry.x + local] == scene.indices[index * 3 + corner],
                    "Cooked meshlet does not reconstruct the original triangle");
        }
    }
    scene.indices[0] = 20;
    bool rejected    = false;
    try
    {
        buildMeshlets(scene);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "Meshlet cooking accepted an out-of-range vertex");
    require(buildMeshlets({}).meshlets.empty(), "Empty meshlet input created geometry");
    std::cout << "CPU meshlet cooking is deterministic and requires no GPU\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
