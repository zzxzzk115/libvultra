#include "support/visibility_raster_probe.hpp"

#include <cstdlib>
#include <optional>

int main(int argc, char** argv)
{
    std::optional<vultra::ShaderProgram> uncoated;
    std::optional<vultra::ShaderProgram> sceneLights;
    while (argc >= 3)
    {
        const std::string_view flag = argv[argc - 2];
        if (flag != "--uncoated-resolve" && flag != "--scene-lights-resolve")
        {
            break;
        }
        const char*   path = argv[argc - 1];
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() <= 0)
        {
            std::fprintf(stderr, "Cannot open optional resolve shader\n");
            return 1;
        }
        std::vector<std::byte> bytes(static_cast<size_t>(input.tellg()));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        {
            return 1;
        }
        auto program = vultra::ShaderProgram::loadExpected(bytes, path);
        if (!program)
        {
            std::fprintf(stderr, "%s\n", program.error().c_str());
            return 1;
        }
        auto& destination = flag == "--uncoated-resolve" ? uncoated : sceneLights;
        destination.emplace(std::move(*program));
        argc -= 2;
    }
    if (argc != 3 && argc != 4 && argc != 5 && argc != 7)
    {
        std::fprintf(stderr,
                     "Usage: test-visibility-raster raster.vshaderc resolve.vshaderc [shadow.vshaderc "
                     "[gbuffer.vshaderc [sky.vshaderc deferred.vshaderc]]] "
                     "[--uncoated-resolve uncoated.vshaderc] [--scene-lights-resolve scene-lights.vshaderc]\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0)
    {
        std::fprintf(stderr, "Cannot open cooked visibility shader\n");
        return 1;
    }
    std::vector<std::byte> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        return 1;
    auto          shader = vultra::ShaderProgram::loadExpected(bytes, argv[1]);
    std::ifstream resolveInput(argv[2], std::ios::binary | std::ios::ate);
    if (!resolveInput || resolveInput.tellg() <= 0)
        return 1;
    std::vector<std::byte> resolveBytes(static_cast<size_t>(resolveInput.tellg()));
    resolveInput.seekg(0);
    if (!resolveInput.read(reinterpret_cast<char*>(resolveBytes.data()), std::streamsize(resolveBytes.size())))
        return 1;
    auto                                 resolve = vultra::ShaderProgram::loadExpected(resolveBytes, argv[2]);
    std::optional<vultra::ShaderProgram> shadow;
    if (argc >= 4)
    {
        std::ifstream shadowInput(argv[3], std::ios::binary | std::ios::ate);
        if (!shadowInput || shadowInput.tellg() <= 0)
            return 1;
        std::vector<std::byte> shadowBytes(static_cast<size_t>(shadowInput.tellg()));
        shadowInput.seekg(0);
        if (!shadowInput.read(reinterpret_cast<char*>(shadowBytes.data()), std::streamsize(shadowBytes.size())))
            return 1;
        auto program = vultra::ShaderProgram::loadExpected(shadowBytes, argv[3]);
        if (!program)
        {
            std::fprintf(stderr, "%s\n", program.error().c_str());
            return 1;
        }
        shadow.emplace(std::move(*program));
    }
    std::optional<vultra::ShaderProgram> gbuffer;
    if (argc >= 5)
    {
        std::ifstream input(argv[4], std::ios::binary | std::ios::ate);
        if (!input || input.tellg() <= 0)
            return 1;
        std::vector<std::byte> bytes(static_cast<size_t>(input.tellg()));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
            return 1;
        auto program = vultra::ShaderProgram::loadExpected(bytes, argv[4]);
        if (!program)
        {
            std::fprintf(stderr, "%s\n", program.error().c_str());
            return 1;
        }
        gbuffer.emplace(std::move(*program));
    }
    std::array<std::optional<vultra::ShaderProgram>, 2> deferred;
    if (argc == 7)
    {
        for (size_t i = 0; i < deferred.size(); ++i)
        {
            const char*   path = argv[5 + i];
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input || input.tellg() <= 0)
                return 1;
            std::vector<std::byte> bytes(static_cast<size_t>(input.tellg()));
            input.seekg(0);
            if (!input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
                return 1;
            auto program = vultra::ShaderProgram::loadExpected(bytes, path);
            if (!program)
            {
                std::fprintf(stderr, "%s\n", program.error().c_str());
                return 1;
            }
            deferred[i].emplace(std::move(*program));
        }
    }
    auto device = vultra::Device::create(true, nullptr, VriFeature_Bindless);
    if (!shader || !resolve || !device)
    {
        std::fprintf(stderr,
                     "%s\n",
                     !shader  ? shader.error().c_str() :
                     !resolve ? resolve.error().c_str() :
                                device.error().c_str());
        return 1;
    }
    const auto* caps = (*device)->core.GetDeviceDesc((*device)->handle);
    std::printf("Visibility device: geometry=%d tessellation=%d\n", caps->hasGeometryShader, caps->hasTessellation);
    auto result = vultra::testing::visibilityRasterProbe(
        **device,
        *shader,
        *resolve,
        false,
        shadow ? &*shadow : nullptr,
        gbuffer ? &*gbuffer : nullptr,
        deferred[0] ? &*deferred[0] : nullptr,
        deferred[1] ? &*deferred[1] : nullptr,
        std::getenv("VULTRA_TEST_SINGLE_PASS_GBUFFER") != nullptr,
        std::getenv("VULTRA_TEST_GBUFFER_DEPTH32")    ? vultra::GBufferPositionEncoding::eDepth32 :
        std::getenv("VULTRA_TEST_GBUFFER_RELATIVE16") ? vultra::GBufferPositionEncoding::eCameraRelative16 :
                                                        vultra::GBufferPositionEncoding::eWorld32,
        std::getenv("VULTRA_TEST_GBUFFER_POSITION_PROBE") != nullptr,
        uncoated ? &*uncoated : nullptr,
        sceneLights ? &*sceneLights : nullptr);
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    result = vultra::testing::visibilityRasterProbe(**device,
                                                    *shader,
                                                    *resolve,
                                                    true,
                                                    nullptr,
                                                    nullptr,
                                                    nullptr,
                                                    nullptr,
                                                    false,
                                                    vultra::GBufferPositionEncoding::eWorld32,
                                                    false,
                                                    uncoated ? &*uncoated : nullptr,
                                                    sceneLights ? &*sceneLights : nullptr);
    if (!result)
    {
        std::fprintf(stderr, "Batched: %s\n", result.error().c_str());
        return 1;
    }
    std::puts("Indexed visibility raster IDs, alpha masking and HDR material resolve passed");
    return 0;
}
