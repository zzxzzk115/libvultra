#include <vultra/assets/material_block.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#ifdef VULTRA_WITH_SHADER_COMPILER
#include <vultra/drivers/rhi/shader_compiler.hpp>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;
    constexpr Extent kSize {19, 11};
    const std::array entries {ShaderEntry {"main", VriShaderStage_Compute}};

    ShaderParameter scalar(std::string name, ShaderScalarType type, uint64_t offset)
    {
        ShaderParameter result;
        result.name   = std::move(name);
        result.kind   = ShaderParameterKind::eScalar;
        result.scalar = type;
        result.size   = type == ShaderScalarType::eFloat16 ? 2 : 4;
        result.offsets.push_back({ShaderOffsetKind::eUniform, offset, 0});
        return result;
    }

    Result cpu()
    {
        auto vector         = scalar("vector", ShaderScalarType::eFloat16, 4);
        vector.kind         = ShaderParameterKind::eVector;
        vector.columns      = 3;
        vector.size         = 6;
        auto matrix         = scalar("matrix", ShaderScalarType::eFloat16, 24);
        matrix.kind         = ShaderParameterKind::eMatrix;
        matrix.rows         = 2;
        matrix.columns      = 3;
        matrix.columnMajor  = true;
        matrix.matrixStride = 8;
        matrix.size         = 24;
        ShaderParameter array;
        array.name   = "weights";
        array.kind   = ShaderParameterKind::eArray;
        array.size   = 6;
        array.count  = 3;
        array.stride = 2;
        array.offsets.push_back({ShaderOffsetKind::eUniform, 16, 0});
        array.fields = {scalar("$element", ShaderScalarType::eFloat16, 0)};
        ShaderParameter root;
        root.kind   = ShaderParameterKind::eStruct;
        root.size   = 64;
        root.fields = {scalar("a", ShaderScalarType::eFloat16, 0),
                       scalar("b", ShaderScalarType::eFloat16, 2),
                       vector,
                       scalar("tail", ShaderScalarType::eFloat, 12),
                       array,
                       matrix,
                       scalar("enabled", ShaderScalarType::eBoolean, 48)};
        auto block  = ShaderUniformBlock::create(root);
        if (!block)
        {
            return std::unexpected(block.error());
        }
        // Preserve payloads exactly, including negative zero, a NaN and the least positive subnormal.
        const std::array a {ShaderFloat16 {0x8000}};
        const std::array b {ShaderFloat16 {0x7e01}};
        const std::array v {ShaderFloat16 {0x0001}, ShaderFloat16 {0x7bff}, ShaderFloat16 {0x3c00}};
        const std::array m {ShaderFloat16 {0x3c00},
                            ShaderFloat16 {0x4000},
                            ShaderFloat16 {0x4200},
                            ShaderFloat16 {0x4400},
                            ShaderFloat16 {0x4500},
                            ShaderFloat16 {0x4600}};
        const std::array weight {ShaderFloat16 {0x3555}};
        const std::array tail {2.f};
        const std::array enabled {true};
        const std::array edits {ShaderUniformWrite {"a", std::span(a)},
                                ShaderUniformWrite {"b", std::span(b)},
                                ShaderUniformWrite {"vector", std::span(v), ShaderParameterKind::eVector, 1, 3},
                                ShaderUniformWrite {"matrix", std::span(m), ShaderParameterKind::eMatrix, 2, 3},
                                ShaderUniformWrite {"weights[1]", std::span(weight)},
                                ShaderUniformWrite {"tail", std::span(tail)},
                                ShaderUniformWrite {"enabled", std::span(enabled)}};
        auto             changed = block->setExpected(edits);
        if (!changed || !*changed || block->revision() != 1)
        {
            return std::unexpected("Half edit was not accepted once");
        }
        std::array<std::byte, 64> expected {};
        const auto                put = [&](size_t offset, auto value)
        {
            std::memcpy(expected.data() + offset, &value, sizeof(value));
        };
        put(0, uint16_t(0x8000));
        put(2, uint16_t(0x7e01));
        put(4, uint16_t(0x0001));
        put(6, uint16_t(0x7bff));
        put(8, uint16_t(0x3c00));
        put(12, 2.f);
        put(18, uint16_t(0x3555));
        put(24, uint16_t(0x3c00));
        put(26, uint16_t(0x4400));
        put(32, uint16_t(0x4000));
        put(34, uint16_t(0x4500));
        put(40, uint16_t(0x4200));
        put(42, uint16_t(0x4600));
        put(48, uint32_t(1));
        if (!std::ranges::equal(block->data(), expected))
        {
            return std::unexpected("Half packing differs from byte oracle");
        }
        auto identical = block->setExpected(edits);
        if (!identical || *identical || block->revision() != 1)
        {
            return std::unexpected("Identical half payload changed");
        }
        const std::array         wrong {ShaderUniformWrite {"a", std::span(tail)}};
        const std::array         replacement {ShaderFloat16 {0x3c00}};
        const ShaderUniformWrite first {"a", std::span(replacement)};
        const std::array         late {first, wrong[0]};
        const std::array         overlap {first, first};
        if (block->setExpected(late) || block->setExpected(overlap) || block->revision() != 1 ||
            !std::ranges::equal(block->data(), expected))
        {
            return std::unexpected("Rejected half edit changed accepted bytes");
        }
        for (int index = 0; index < 5; ++index)
        {
            auto invalid = root;
            if (index == 0)
            {
                invalid.fields[1].offsets[0].offset = 1;
            }
            if (index == 1)
            {
                invalid.fields[3].offsets[0].offset = 10;
            }
            if (index == 2)
            {
                invalid.fields[4].stride = 3;
            }
            if (index == 3)
            {
                invalid.fields[5].matrixStride = 3;
            }
            if (index == 4)
            {
                invalid.fields[0].size = 4;
            }
            if (ShaderUniformBlock::create(invalid))
            {
                return std::unexpected("Malformed mixed-width layout accepted");
            }
            std::string diagnostic = "retained";
            if (block->reconcileExpected(invalid, diagnostic) || diagnostic != "retained")
            {
                return std::unexpected("Rejected half reconcile changed diagnostics");
            }
        }
        auto next                        = root;
        next.fields[0].offsets[0].offset = 10;
        next.fields[1].offsets[0].offset = 12;
        next.fields[3].offsets[0].offset = 0;
        next.fields[4].count             = 4;
        next.fields[4].stride            = 4;
        next.fields[4].size              = 16;
        next.fields[5].offsets[0].offset = 32;
        next.fields[5].columnMajor       = false;
        next.fields[5].size              = 16;
        next.fields[6].offsets[0].offset = 56;
        std::string diagnostic;
        auto        migrated = block->reconcileExpected(next, diagnostic);
        if (!migrated || diagnostic.find("Resized uniform array") == std::string::npos)
        {
            return std::unexpected("Half layout reconciliation failed");
        }
        expected.fill(std::byte(0));
        put(0, 2.f);
        put(4, uint16_t(0x0001));
        put(6, uint16_t(0x7bff));
        put(8, uint16_t(0x3c00));
        put(10, uint16_t(0x8000));
        put(12, uint16_t(0x7e01));
        put(20, uint16_t(0x3555));
        put(32, uint16_t(0x3c00));
        put(34, uint16_t(0x4000));
        put(36, uint16_t(0x4200));
        put(40, uint16_t(0x4400));
        put(42, uint16_t(0x4500));
        put(44, uint16_t(0x4600));
        put(56, uint32_t(1));
        if (!std::ranges::equal(migrated->data(), expected) || migrated->revision() != 2)
        {
            return std::unexpected("Half migration did not preserve payloads, matrix order and zero padding");
        }
        auto widened             = root;
        widened.fields[0].scalar = ShaderScalarType::eFloat;
        widened.fields[0].size   = 4;
        widened.fields.erase(widened.fields.begin() + 1);
        diagnostic.clear();
        auto reset = block->reconcileExpected(widened, diagnostic);
        if (!reset || diagnostic.find("Reset incompatible uniform field: a") == std::string::npos ||
            !std::ranges::all_of(reset->data().first(4),
                                 [](std::byte b)
                                 {
                                     return b == std::byte(0);
                                 }))
        {
            return std::unexpected("Half-to-float layout change implicitly converted a value");
        }
        return {};
    }

    Result initialize(MaterialBlock& block)
    {
        const std::array gain {ShaderFloat16 {0x4000}};
        const std::array bias {ShaderFloat16 {0x3400}};
        const std::array tint {ShaderFloat16 {0x3c00},
                               ShaderFloat16 {0x3800},
                               ShaderFloat16 {0x4000},
                               ShaderFloat16 {0x3800}};
        const std::array weights0 {ShaderFloat16 {0x3800}};
        const std::array weights1 {ShaderFloat16 {0x3c00}};
        const std::array basis {ShaderFloat16 {0x3c00},
                                ShaderFloat16 {0x4000},
                                ShaderFloat16 {0x4200},
                                ShaderFloat16 {0x4400},
                                ShaderFloat16 {0x4500},
                                ShaderFloat16 {0x4600}};
        const std::array tail {0.25f};
        const std::array enabled {1u};
        const std::array fields {ShaderUniformWrite {"gain", std::span(gain)},
                                 ShaderUniformWrite {"bias", std::span(bias)},
                                 ShaderUniformWrite {"tint", std::span(tint), ShaderParameterKind::eVector, 1, 4},
                                 ShaderUniformWrite {"weights[0]", std::span(weights0)},
                                 ShaderUniformWrite {"weights[1]", std::span(weights1)},
                                 ShaderUniformWrite {"basis", std::span(basis), ShaderParameterKind::eMatrix, 2, 3},
                                 ShaderUniformWrite {"tail", std::span(tail)},
                                 ShaderUniformWrite {"enabled", std::span(enabled)}};
        const std::array writes {MaterialUniformWrite {"material", fields}};
        auto             changed = block.setExpected(writes);
        if (!changed || !*changed)
        {
            return std::unexpected(changed ? "Half initialization unchanged" : changed.error());
        }
        return {};
    }

    struct Gpu
    {
        std::unique_ptr<ShaderBindings> bindings;
        std::unique_ptr<ShaderPipeline> pipeline;
    };

    std::expected<Gpu, std::string>
    prepare(Device& device, const ShaderProgram& program, const MaterialBlock& block, const VriDescriptor* output)
    {
        const std::array<std::string_view, 1> names {"material"};
        auto                                  bindings = ShaderBindings::create(device, program, entries, names);
        if (!bindings)
        {
            return std::unexpected(bindings.error());
        }
        Gpu              gpu {std::move(*bindings), {}};
        const std::array resources {ShaderResourceViews {"output", VriDescriptorType_StorageTexture, {output}}};
        const std::array uniforms {ShaderUniformUpload {"material", block.uniforms().begin()->second}};
        if (auto updated = gpu.bindings->updateExpected(resources, uniforms); !updated)
        {
            return std::unexpected(updated.error());
        }
        auto pipeline = ShaderPipeline::create(
            device,
            program,
            {entries.begin(), entries.end()},
            [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                VriPipeline*                 result = nullptr;
                const VriComputePipelineDesc desc {gpu.bindings->layout(), shaders.front(), device.pipelineCache};
                const auto                   status = device.core.CreateComputePipeline(device.handle, &desc, &result);
                if (status != VriResult_Success || !result)
                {
                    if (result)
                    {
                        device.core.DestroyPipeline(result);
                    }
                    return std::unexpected("Create native half-uniform pipeline failed");
                }
                return result;
            });
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        gpu.pipeline = std::move(*pipeline);
        return gpu;
    }

    Result gpu(const ShaderProgram& oldProgram, const ShaderProgram& newProgram)
    {
        auto block = MaterialBlock::create(oldProgram, "material");
        if (!block)
        {
            return std::unexpected(block.error());
        }
        if (auto populated = initialize(*block); !populated)
        {
            return populated;
        }
        auto text = block->serializeExpected();
        if (!text)
        {
            return std::unexpected(text.error());
        }
        auto restored = MaterialBlock::parseExpected(*text);
        if (!restored || restored->serializeExpected() != text)
        {
            return std::unexpected("Half snapshot roundtrip failed");
        }
        block       = std::move(restored);
        auto device = Device::create();
        if (!device)
        {
            return std::unexpected(device.error());
        }
        auto& native = **device;
        auto  desc   = colorTexture(kSize, VriFormat_RGBA32_SFLOAT);
        desc.usage   = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferSrc;
        auto output  = Texture::create(native, desc);
        if (!output)
        {
            return std::unexpected(output.error());
        }
        auto view = (*output)->viewExpected();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        auto active = prepare(native, oldProgram, *block, *view);
        if (!active)
        {
            return std::unexpected(active.error());
        }
        auto frame = Frame::create(native);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto render = [&](Gpu& current, const std::array<float, 4>& color) -> Result
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            VriTextureViewDesc full {};
            full.viewType = VriTextureViewType_2D;
            full.aspect   = VriImageAspect_Color;
            if (auto transitioned = (*output)->transitionExpected(*commands,
                                                                  {VriAccess_ShaderResourceStorageWrite,
                                                                   VriLayout_ShaderResourceStorage,
                                                                   VriPipelineStage_ComputeShader},
                                                                  full);
                !transitioned)
            {
                return transitioned;
            }
            auto uniform = current.bindings->uniformBufferExpected("material");
            if (!uniform)
            {
                return std::unexpected(uniform.error());
            }
            if (auto transitioned =
                    (*uniform)->transitionExpected(*commands,
                                                   {VriAccess_ConstantBufferRead, VriPipelineStage_ComputeShader});
                !transitioned)
            {
                return transitioned;
            }
            if (auto bound = current.bindings->bindExpected(*commands, current.pipeline->handle()); !bound)
            {
                return bound;
            }
            const VriDispatchDesc dispatch {3, 2, 1};
            native.core.CmdDispatch(*commands, &dispatch);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            auto image = readbackExpected(native, **output);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            for (uint32_t y = 0; y < kSize.height; ++y)
            {
                for (uint32_t x = 0; x < kSize.width; ++x)
                {
                    const std::array expected {color[0] + float(x) / 32, color[1] + float(y) / 32, color[2], color[3]};
                    for (size_t c = 0; c < 4; ++c)
                    {
                        if (image->rgba[(size_t(y) * kSize.width + x) * 4 + c] != expected[c])
                        {
                            return std::unexpected("Half uniform/matrix GPU pixel differs from independent oracle");
                        }
                    }
                }
            }
            return {};
        };
        if (auto checked = render(*active, {12, 22.25f, 8.5f, 0.5f}); !checked)
        {
            return checked;
        }
        const auto       storage  = active->bindings->uniformBufferExpected("material");
        const auto       pipeline = active->pipeline->handle();
        const std::array gain {ShaderFloat16 {0x3c00}};
        const std::array fields {ShaderUniformWrite {"gain", std::span(gain)}};
        const std::array edits {MaterialUniformWrite {"material", fields}};
        if (!block->setExpected(edits))
        {
            return std::unexpected("Edit live binary16 gain failed");
        }
        const std::array resources {ShaderResourceViews {"output", VriDescriptorType_StorageTexture, {*view}}};
        const std::array uniforms {ShaderUniformUpload {"material", block->uniforms().begin()->second}};
        if (auto updated = active->bindings->updateExpected(resources, uniforms); !updated)
        {
            return updated;
        }
        if (active->bindings->uniformBufferExpected("material") != storage || active->pipeline->handle() != pipeline)
        {
            return std::unexpected("Half numeric edit recreated native storage/pipeline");
        }
        if (auto checked = render(*active, {11, 21.75f, 6.5f, 0.5f}); !checked)
        {
            return checked;
        }
        if (active->bindings->validateLayoutExpected(newProgram, entries))
        {
            return std::unexpected("Changed half layout accepted by old bindings");
        }
        std::string diagnostic;
        auto        candidate = block->reconcileExpected(newProgram, diagnostic);
        if (!candidate)
        {
            return std::unexpected(candidate.error());
        }
        auto replacement = prepare(native, newProgram, *candidate, *view);
        if (!replacement)
        {
            return std::unexpected(replacement.error());
        }
        if (auto checked = render(*replacement, {11, 21.75f, 6.5f, 0.5f}); !checked)
        {
            return checked;
        }
        if (auto retained = render(*active, {11, 21.75f, 6.5f, 0.5f}); !retained)
        {
            return retained;
        }
        return {};
    }

    std::expected<std::array<ShaderProgram, 2>, std::string> programsExpected(int argc, char** argv)
    {
        std::array<ShaderProgram, 2> programs;
        if (argc == 3)
        {
            for (size_t i = 0; i < programs.size(); ++i)
            {
                auto loaded = ShaderProgram::loadExpected(argv[i + 1]);
                if (!loaded)
                {
                    return std::unexpected(loaded.error());
                }
                programs[i] = std::move(*loaded);
            }
            return programs;
        }
#ifdef VULTRA_WITH_SHADER_COMPILER
        if (argc == 1)
        {
            auto compiler = ShaderCompiler::create({});
            if (!compiler)
            {
                return std::unexpected(compiler.error());
            }
            ShaderCompileOptions options;
            options.entries.assign(entries.begin(), entries.end());
            for (size_t i = 0; i < programs.size(); ++i)
            {
                options.defines = {{"NEW_LAYOUT", std::to_string(i)}};
                auto compiled   = (*compiler)->compileExpected("tests/shaders/shader_half.slang", options);
                if (!compiled)
                {
                    return std::unexpected(compiled.error());
                }
                programs[i] = std::move(*compiled);
            }
            return programs;
        }
#endif
        return std::unexpected("Usage: test-shader-half old.vshaderc new.vshaderc");
    }
} // namespace

int main(int argc, char** argv)
{
    auto programs = programsExpected(argc, argv);
    if (!programs)
    {
        std::fprintf(stderr, "%s\n", programs.error().c_str());
        return 1;
    }
    if (auto checked = cpu(); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    if (auto checked = gpu((*programs)[0], (*programs)[1]); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    std::printf("SHADER_HALF pixels=209 layouts=2 mixed_width=1 matrix_repack=1 exact_payload=1 persistence=1 "
                "numeric_reuse=1\n");
    return 0;
}
