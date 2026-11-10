#include <vultra/assets/asset_source.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/servers/rendering/builtin/tone_mapping_pass.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;

    std::expected<Image, std::string> render(Device& device, ToneMappingPass& pass, Texture& input)
    {
        RenderGraph graph(device);
        auto        source = graph.importResourceExpected("hdr", input);
        if (!source)
        {
            return std::unexpected(source.error());
        }
        const std::array inputs {*source};
        const std::array parameters {0.0, 0.0, 1.0};
        auto             outputs = pass.addPassesExpected(graph, "tone", inputs, parameters);
        if (!outputs)
        {
            return std::unexpected(outputs.error());
        }
        if (auto exported = graph.exportResourceExpected(outputs->front()); !exported)
        {
            return std::unexpected(exported.error());
        }
        if (auto compiled = graph.compileExpected(); !compiled)
        {
            return std::unexpected(compiled.error());
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto recorded = graph.executeExpected(*commands); !recorded)
        {
            return std::unexpected(recorded.error());
        }
        if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        auto output = graph.getTextureExpected(outputs->front());
        if (!output)
        {
            return std::unexpected(output.error());
        }
        return readbackExpected(device, **output);
    }

    Result pixelsExpected(const Image& image, float alpha)
    {
        const std::array expected {0.125f, 0.5f, 2.0f, alpha};
        if (image.size != Extent {5, 3} || image.rgba.size() != 60)
        {
            return std::unexpected("Tone mapping fixture readback has the wrong extent");
        }
        for (size_t i = 0; i < image.rgba.size(); ++i)
        {
            if (image.rgba[i] != expected[i % 4])
            {
                return std::unexpected("Tone mapping reflection/reload changed exact linear fixture pixels");
            }
        }
        return {};
    }

    Result run(Device& device, const std::array<ShaderProgram, 4>& programs, const std::filesystem::path& directory)
    {
        auto bindings = programs[1].resourceBindingsExpected();
        if (!bindings)
        {
            return std::unexpected(bindings.error());
        }
        const auto shifted = std::ranges::find(*bindings, std::string_view("hdrColor"), &ShaderResourceBinding::name);
        if (shifted == bindings->end() || shifted->set != 3 || shifted->binding != 5 ||
            programs[0].parameters == programs[1].parameters || programs[0].parameters == programs[2].parameters ||
            programs[0].parameters != programs[3].parameters)
        {
            return std::unexpected(
                "Tone fixtures do not isolate relocated bindings, changed types and compatible code");
        }
        const auto      file = directory / "builtin/shaders/passes/tone_mapping.vshaderc";
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        if (error)
        {
            return std::unexpected("Create tone fixture source directory: " + error.message());
        }
        auto source = AssetSource::create(directory);
        if (!source)
        {
            return std::unexpected(source.error());
        }
        TextureData data;
        data.format = TextureFormat::eRgba32Sfloat;
        TextureLevel level;
        level.size = {5, 3};
        std::array<float, 60> values;
        const std::array      color {0.125f, 0.5f, 2.0f, 1.0f};
        for (size_t i = 0; i < values.size(); ++i)
        {
            values[i] = color[i % 4];
        }
        level.bytes.resize(sizeof(values));
        std::memcpy(level.bytes.data(), values.data(), sizeof(values));
        data.levels.push_back(std::move(level));
        auto input = uploadTextureAssetExpected(device, data);
        if (!input)
        {
            return std::unexpected(input.error());
        }
        if (auto installed = programs[1].saveExpected(file); !installed)
        {
            return installed;
        }
        {
            auto pass = ToneMappingPass::create(device, VriFormat_RGBA16_SFLOAT, source->get());
            if (!pass)
            {
                return std::unexpected(pass.error());
            }
            auto image = render(device, **pass, **input);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            if (auto checked = pixelsExpected(*image, 1); !checked)
            {
                return checked;
            }
        }
        if (auto installed = programs[2].saveExpected(file); !installed)
        {
            return installed;
        }
        auto rejected = ToneMappingPass::create(device, VriFormat_RGBA16_SFLOAT, source->get());
        if (rejected || rejected.error().find("Tone mapping shader parameters") == std::string::npos)
        {
            return std::unexpected("Same-sized uint4 tone parameters were not rejected by typed initialization");
        }
        if (auto installed = programs[0].saveExpected(file); !installed)
        {
            return installed;
        }
        auto pass = ToneMappingPass::create(device, VriFormat_RGBA16_SFLOAT, source->get());
        if (!pass)
        {
            return std::unexpected(pass.error());
        }
        auto initial = render(device, **pass, **input);
        if (!initial)
        {
            return std::unexpected(initial.error());
        }
        if (auto checked = pixelsExpected(*initial, 1); !checked)
        {
            return checked;
        }
        auto&      shader     = (*pass)->shader();
        const auto handle     = shader.handle();
        const auto generation = shader.generation();
        const auto hash       = shader.identity().bytecodeHash;
        for (size_t variant = 1; variant <= 2; ++variant)
        {
            if (auto installed = programs[variant].saveExpected(file); !installed)
            {
                return installed;
            }
            if (auto reload = shader.reloadExpected();
                reload || shader.handle() != handle || shader.generation() != generation ||
                shader.identity().bytecodeHash != hash ||
                reload.error().find("Reflected shader layout changed") == std::string::npos)
            {
                return std::unexpected("Rejected tone reload replaced an accepted pipeline/layout");
            }
            auto image = render(device, **pass, **input);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            if (auto checked = pixelsExpected(*image, 1); !checked)
            {
                return checked;
            }
        }
        if (auto installed = programs[3].saveExpected(file); !installed)
        {
            return installed;
        }
        if (auto reload = shader.reloadExpected();
            !reload || shader.generation() != generation + 1 || shader.identity().bytecodeHash == hash)
        {
            return std::unexpected(reload ? "Compatible tone reload did not change code/generation" : reload.error());
        }
        auto changed = render(device, **pass, **input);
        if (!changed)
        {
            return std::unexpected(changed.error());
        }
        if (auto checked = pixelsExpected(*changed, 0.5f); !checked)
        {
            return checked;
        }
        if (auto installed = programs[0].saveExpected(file); !installed)
        {
            return installed;
        }
        if (auto reload = shader.reloadExpected();
            !reload || shader.generation() != generation + 2 || shader.identity().bytecodeHash != hash)
        {
            return std::unexpected(reload ? "Tone reload recovery lost the original code" : reload.error());
        }
        auto restored = render(device, **pass, **input);
        if (!restored)
        {
            return std::unexpected(restored.error());
        }
        if (auto checked = pixelsExpected(*restored, 1); !checked)
        {
            return checked;
        }
        std::puts("TONE_REFLECTED exact_channels=360 relocated_set=3 relocated_binding=5 typed_uint4_rejected=1 "
                  "reload_layout_rejected=2 compatible_alpha=0.5 recovered_alpha=1 cooked_only=1");
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr,
                     "Usage: test-tone-mapping-bindings base.vshaderc shifted.vshaderc "
                     "uint4.vshaderc compatible.vshaderc writable-root\n");
        return 1;
    }
    std::array<vultra::ShaderProgram, 4> programs;
    for (size_t i = 0; i < programs.size(); ++i)
    {
        auto program = vultra::ShaderProgram::loadExpected(argv[i + 1]);
        if (!program)
        {
            std::fprintf(stderr, "%s\n", program.error().c_str());
            return 1;
        }
        programs[i] = std::move(*program);
    }
    auto device = vultra::Device::create(true);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto checked = run(**device, programs, argv[5]); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
