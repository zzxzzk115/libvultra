#include "../examples/common/triangle.hpp"

#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <chrono>
#include <cstdio>
#include <fstream>
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

    template<class Callback>
    void reject(Callback&& callback)
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
        require(rejected, "Expected shader input rejection");
    }

    std::vector<uint8_t> readBytes(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary);
        require(bool(input), "Open test shader artifact");
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    // Alter a valid artifact's metadata while retaining its checksum to exercise schema checks.
    template<class Callback>
    void alterDocument(const std::filesystem::path& file, const std::vector<uint8_t>& original, Callback&& callback)
    {
        auto document = nlohmann::json::from_cbor(original.begin() + 16, original.end());
        callback(document);
        const auto           payload = nlohmann::json::to_cbor(document);
        std::vector<uint8_t> bytes(original.begin(), original.begin() + 16);
        const auto           hash = XXH3_64bits(payload.data(), payload.size());
        for (size_t i = 0; i < sizeof(hash); ++i)
        {
            bytes[8 + i] = uint8_t(hash >> (8 * i));
        }
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        vultra::writeFileAtomically(file, std::as_bytes(std::span(bytes)));
        reject(
            [&]
            {
                vultra::ShaderProgram::load(file);
            });
    }
} // namespace

int main()
try
{
    using namespace vultra;
    namespace fs    = std::filesystem;
    const auto root = fs::path("build/.tmp/shader-program") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root / "source");
    const auto source = root / "source/triangle.slang";
    const auto cooked = root / "source/triangle.vshaderc";
    fs::copy_file("examples/research/shaders/triangle.slang", source);
    fs::copy_file("examples/research/shaders/color.slangh", root / "source/color.slangh");
    const std::array includes {fs::path("builtin/shaders"), fs::path("examples/common")};
    const std::array entries {ShaderEntry {"vertexMain", VriShaderStage_Vertex},
                              ShaderEntry {"fragmentMain", VriShaderStage_Fragment}};
    const auto       program = ShaderProgram::compile(source, {}, includes);
    require(program.shaders.size() == 2 && !program.rayQuery, "Shader entry discovery failed");
    program.save(cooked);
    ShaderCompileOptions cacheOptions;
    cacheOptions.includeDirectories.assign(includes.begin(), includes.end());
    require(!ShaderProgram::cook(source, cooked, cacheOptions), "Raw Slang unchanged cache missed");
    auto defineOptions = cacheOptions;
    defineOptions.defines.push_back({"TEST_CACHE_DEFINE", "1"});
    require(ShaderProgram::cook(source, cooked, defineOptions) && !ShaderProgram::cook(source, cooked, defineOptions),
            "Macro options did not invalidate/cache raw Slang");
    program.save(cooked);
    const auto original = readBytes(cooked);
    const auto restored = ShaderProgram::load(cooked);
    require(restored.shaders.size() == program.shaders.size() &&
                restored.shaders[0].words == program.shaders[0].words &&
                restored.shaders[1].words == program.shaders[1].words,
            "Cooked bytecode changed during roundtrip");
    const auto views = restored.descriptors(entries);
    require(views.size() == 2 && std::string_view(views[0].entryPointName) == "vertexMain" &&
                views[1].stage == VriShaderStage_Fragment,
            "Cooked descriptor selection failed");
    const std::array wrongStage {ShaderEntry {"vertexMain", VriShaderStage_Fragment}};
    reject(
        [&]
        {
            ShaderProgram::compile(source, wrongStage, includes);
        });
    reject(
        [&]
        {
            restored.descriptors(wrongStage);
        });
    const std::array missing {ShaderEntry {"missing", VriShaderStage_Vertex}};
    reject(
        [&]
        {
            restored.descriptors(missing);
        });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["version"] = 2;
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["target"] = "dxil";
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["entries"][0]["stage"] = uint64_t(0x100000001);
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["entries"][1]["name"] = "vertexMain";
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["entries"][0]["code"] = nlohmann::json::binary({1, 2, 3});
                  });
    writeFileAtomically(cooked, std::as_bytes(std::span(original)));
    auto invalid                = program;
    invalid.shaders[0].words[0] = 0;
    reject(
        [&]
        {
            invalid.save(cooked);
        });
    require(readBytes(cooked) == original, "Rejected shader save overwrote last good artifact");

    Device      device;
    Frame       frame(device);
    Texture     target(device, colorTexture({129, 73}, VriFormat_BGRA8_UNORM));
    Image       baseline;
    const float clear[4] {0.1f, 0.2f, 0.7f, 1};
    auto        render = [&](Triangle& triangle)
    {
        auto* commands = frame.begin();
        target.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        triangle.draw(commands, target, clear);
        frame.submitAndWait();
        return readback(device, target);
    };
    {
        Triangle triangle(device, VriFormat_BGRA8_UNORM, source, root / "source", {includes.begin(), includes.end()});
        baseline = render(triangle);
    }
    fs::remove(source);
    fs::remove(root / "source/color.slangh");
    // The ordinary source path resolves its cooked sibling, without includes or an existing watch directory.
    Triangle shipped(device, VriFormat_BGRA8_UNORM, source, root / "missing-watch-root", {});
    require(!shipped.pipeline->poll(), "Cooked program unexpectedly created a watcher");
    require(compare(baseline, render(shipped)).mse == 0, "Source and source-free cooked GPU pixels differ");
    const auto handle     = shipped.pipeline->handle();
    const auto generation = shipped.pipeline->generation();
    auto       corrupt    = original;
    corrupt.back() ^= 1;
    writeFileAtomically(cooked, std::as_bytes(std::span(corrupt)));
    require(!shipped.pipeline->reload() && shipped.pipeline->handle() == handle &&
                shipped.pipeline->generation() == generation,
            "Corrupt cooked replacement destroyed last good pipeline");
    require(shipped.pipeline->diagnostics().find("checksum") != std::string::npos &&
                compare(baseline, render(shipped)).mse == 0,
            "Cooked failure lost diagnostics or changed image");
    writeFileAtomically(cooked, std::as_bytes(std::span(original)));
    require(shipped.pipeline->reload() && shipped.pipeline->generation() == generation + 1 &&
                compare(baseline, render(shipped)).mse == 0,
            "Cooked replacement recovery failed");
    Triangle explicitCooked(device, VriFormat_BGRA8_UNORM, cooked, {}, {});
    require(compare(baseline, render(explicitCooked)).mse == 0, "Explicit cooked file changed pixels");
    std::puts("Shader cooking passed: source-free GPU parity, metadata validation and replacement failure/recovery");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "Shader program test: %s\n", error.what());
    return 1;
}
