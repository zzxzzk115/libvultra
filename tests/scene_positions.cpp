#include "support/builtin_frame_expected_probe.hpp"

#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/scene_render_data.hpp>

namespace
{
    std::expected<void, std::string> run(const char* path)
    {
        using namespace vultra;
        auto archive = VpkArchive::open(path);
        if (!archive)
        {
            return std::unexpected(archive.error());
        }
        auto shaders = AssetSource::create(std::move(*archive));
        if (!shaders)
        {
            return std::unexpected(shaders.error());
        }
        auto bytes = (*shaders)->readExpected("builtin/shaders/passes/environment.vshaderc");
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        auto program  = ShaderProgram::loadExpected(*bytes, path);
        auto radiance = decodeEnvironmentExpected({}, EnvironmentPrecision::ePreferFloat16);
        auto device   = Device::create(true);
        if (!program)
        {
            return std::unexpected(program.error());
        }
        if (!radiance)
        {
            return std::unexpected(radiance.error());
        }
        if (!device)
        {
            return std::unexpected(device.error());
        }
        auto environment = Environment::create(**device, *radiance, &*program);
        if (!environment)
        {
            return std::unexpected(environment.error());
        }
        SceneData data;
        data.vertices   = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}},
                           {{1, -1, 0}, {0, 0, 1}, {1, 0}},
                           {{1, 1, 0}, {0, 0, 1}, {1, 1}},
                           {{-1, 1, 0}, {0, 0, 1}, {0, 1}}};
        data.indices    = {0, 1, 2, 0, 2, 3};
        data.primitives = {{0, 6, 0}};
        data.materials.emplace_back();
        data.radius = std::sqrt(2.f);
        PreparedTextures textures;
        textures.images.push_back({TextureFormat::eRgba8Unorm, {{{1, 1}, std::vector<std::byte>(4, std::byte(255))}}});
        textures.materials.emplace_back();
        auto scene = GpuScene::create(**device, data, textures);
        if (!scene)
        {
            return std::unexpected(scene.error());
        }
        return test::builtinFrameExpectedProbe(**device, **scene, **environment, shaders->get(), data.vertices);
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fputs("Usage: test-scene-positions builtin-shaders.vpk\n", stderr);
        return 2;
    }
    if (auto result = run(argv[1]); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
