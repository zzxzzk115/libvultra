#include <vultra/assets/asset_source.hpp>
#include <vultra/drivers/rhi/buffer_readback.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/shader_runtime.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;

    Result checkMetadata(const ShaderAsset& asset, bool remapped = false)
    {
        if (asset.variants.size() != 3)
        {
            return std::unexpected("Material conformance variants were lost");
        }
        for (const auto& variant : asset.variants)
        {
            const bool  single       = variant.name == "OnlyAdd";
            const bool  swapped      = variant.name == "Swapped" || (variant.name == "Default" && remapped);
            const auto& conformances = variant.typeConformances;
            if (conformances.size() != (single ? 2 : 3))
            {
                return std::unexpected("Effective variant conformances lost global/declared inputs");
            }
            const auto find = [&](std::string_view type, std::string_view interfaceType, uint32_t id)
            {
                return std::ranges::any_of(conformances,
                                           [&](const auto& c)
                                           {
                                               return c.typeName == type && c.interfaceType == interfaceType &&
                                                      c.id == id;
                                           });
            };
            if (!find("AddModel", "IMarker", 3) || !find("AddModel", "IMaterialModel", swapped ? 11 : 3) ||
                (!single && !find("MultiplyModel", "IMaterialModel", swapped ? 3 : 11)))
            {
                return std::unexpected("Effective variant dispatch IDs differ from the compiled selection");
            }
        }
        return {};
    }

#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
    std::expected<std::string, std::string> readText(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary);
        if (!input)
        {
            return std::unexpected("Open conformance fixture: " + file.string());
        }
        std::string text {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad())
        {
            return std::unexpected("Read conformance fixture: " + file.string());
        }
        return text;
    }

    std::expected<std::array<ShaderAsset, 3>, std::string> compileAssets(const std::filesystem::path& root)
    {
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error)
        {
            return std::unexpected(error.message());
        }
        for (const auto* file :
             {"material_type_conformances.vshader", "type_conformance_types.slang", "type_conformance_models.slang"})
        {
            std::filesystem::copy_file(std::filesystem::path("tests/shaders") / file, root / file, error);
            if (error)
            {
                return std::unexpected("Copy material conformance fixture: " + error.message());
            }
        }
        const auto source = root / "material_type_conformances.vshader";
        auto       text   = readText(source);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        ShaderCompileOptions options;
        options.typeConformances      = {{"AddModel", "IMarker", 3}};
        const std::string fromA       = "\"AddModel\" \"IMaterialModel\" = 3";
        const std::string fromB       = "\"MultiplyModel\" \"IMaterialModel\" = 11";
        const auto        declaration = text->find(fromA);
        if (declaration == std::string::npos || text->find(fromB) == std::string::npos)
        {
            return std::unexpected("Material conformance fixture declarations changed");
        }
        std::array<ShaderAsset, 3> assets;
        for (size_t i = 0; i < assets.size(); ++i)
        {
            auto input   = *text;
            auto request = options;
            if (i == 1)
            {
                request.defines = {{"OUTPUT_BIAS", "4"}};
            }
            else if (i == 2)
            {
                input.replace(input.find(fromA), fromA.size(), "\"AddModel\" \"IMaterialModel\" = 11");
                input.replace(input.find(fromB), fromB.size(), "\"MultiplyModel\" \"IMaterialModel\" = 3");
            }
            auto compiled = ShaderAsset::compileSourceExpected(source, input, request);
            if (!compiled)
            {
                return std::unexpected(compiled.error());
            }
            if (auto checked = checkMetadata(*compiled, i == 2); !checked)
            {
                return std::unexpected(checked.error());
            }
            const auto file = root / ("asset-" + std::to_string(i) + ".vshaderc");
            if (auto saved = compiled->saveExpected(file); !saved)
            {
                return std::unexpected(saved.error());
            }
            auto loaded = ShaderAsset::loadExpected(file);
            if (!loaded)
            {
                return std::unexpected(loaded.error());
            }
            if (auto checked = checkMetadata(*loaded, i == 2); !checked)
            {
                return std::unexpected(checked.error());
            }
            assets[i] = std::move(*loaded);
        }
        const auto& pass    = assets[0].subshaders.front().passes.front();
        const auto  base    = pass.programExpected("Default");
        const auto  swapped = pass.programExpected("Swapped");
        if (!base || !swapped)
        {
            return std::unexpected("Missing compiled material conformance program");
        }
        auto baseBindings    = (*base)->resourceBindingsExpected();
        auto swappedBindings = (*swapped)->resourceBindingsExpected();
        if (!baseBindings || !swappedBindings || (*base)->compileKey == (*swapped)->compileKey ||
            std::ranges::any_of(*baseBindings,
                                [](const auto& b)
                                {
                                    return b.name == "audit";
                                }) ||
            !std::ranges::any_of(*swappedBindings,
                                 [](const auto& b)
                                 {
                                     return b.name == "audit";
                                 }))
        {
            return std::unexpected("Material variants lost distinct program keys or descriptor layouts");
        }
        const auto id = declaration + std::strlen("\"AddModel\" \"IMaterialModel\" = ");
        for (const auto* invalid : {"-1", "+3", "3.5", "3e0", "4294967296"})
        {
            auto input = *text;
            input.replace(id, 1, invalid);
            auto rejected = ShaderAsset::parseExpected(source, input);
            if (rejected || rejected.error().find("uint32 decimal integer") == std::string::npos ||
                rejected.error().find(source.string()) == std::string::npos)
            {
                return std::unexpected("Invalid variant dispatch ID lost its source diagnostic");
            }
        }
        auto conflict = options;
        conflict.typeConformances.push_back({"AddModel", "IMaterialModel", 3});
        auto rejected = ShaderAsset::compileSourceExpected(source, *text, conflict);
        if (rejected || rejected.error().find("Variant Default: Duplicate") == std::string::npos)
        {
            return std::unexpected("Global and variant conformance conflict was silently accepted");
        }
        auto unknown = *text;
        unknown.replace(declaration, std::strlen("\"AddModel\""), "\"MissingModel\"");
        const auto archive = root / "asset-0.vshaderc";
        auto       files   = AssetSource::create(root);
        if (!files)
        {
            return std::unexpected(files.error());
        }
        auto original = (*files)->readExpected("asset-0.vshaderc");
        if (!original)
        {
            return std::unexpected(original.error());
        }
        if (auto written = writeFileAtomicallyExpected(source, std::as_bytes(std::span(unknown))); !written)
        {
            return std::unexpected(written.error());
        }
        auto failedCook = ShaderAsset::cookExpected(source, archive, options);
        auto retained   = (*files)->readExpected("asset-0.vshaderc");
        if (failedCook || failedCook.error().find("Pass Evaluate, variant Default") == std::string::npos || !retained ||
            *retained != *original)
        {
            return std::unexpected(
                "Failed conformance cook replaced the accepted archive or lost Pass/variant context");
        }
        if (auto written = writeFileAtomicallyExpected(source, std::as_bytes(std::span(*text))); !written)
        {
            return std::unexpected(written.error());
        }
        auto stripped = ShaderAsset::compileExpected(source, options, std::array<std::string, 1> {"OnlyAdd"});
        if (!stripped || stripped->variants.size() != 1 || stripped->variants[0].typeConformances.size() != 2)
        {
            return std::unexpected("Variant stripping lost the effective conformance selection");
        }
        std::puts("MATERIAL_CONFORMANCE_COMPILER variants=3 global_conflict=1 invalid_ids=5 failed_cook_retained=1 "
                  "stripped=1");
        return assets;
    }
#endif

    struct NativeFault
    {
        Device&                    device;
        VriCoreInterface           original;
        bool                       reject = false;
        std::set<VriPipeline*>     live;
        inline static NativeFault* s_Active = nullptr;

        explicit NativeFault(Device& device) :
            device(device),
            original(device.core)
        {
            s_Active                          = this;
            device.core.CreateComputePipeline = create;
            device.core.DestroyPipeline       = destroy;
        }

        ~NativeFault()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static VriResult VRI_CALL create(VriDevice* device, const VriComputePipelineDesc* desc, VriPipeline** pipeline)
        {
            const auto result = s_Active->original.CreateComputePipeline(device, desc, pipeline);
            if (result == VriResult_Success)
            {
                s_Active->live.insert(*pipeline);
                if (s_Active->reject)
                {
                    return VriResult_OutOfMemory;
                }
            }
            return result;
        }

        static void VRI_CALL destroy(VriPipeline* pipeline)
        {
            s_Active->live.erase(pipeline);
            s_Active->original.DestroyPipeline(pipeline);
        }
    };

    struct Fixture
    {
        Device&                                device;
        std::array<std::unique_ptr<Buffer>, 2> outputs;
        std::array<VriDescriptor*, 2>          views {};
        std::unique_ptr<Frame>                 frame;
        std::unique_ptr<ShaderRuntime>         runtime;
        uint64_t                               channels = 0;

        explicit Fixture(Device& device) :
            device(device)
        {
        }

        ~Fixture()
        {
            runtime.reset();
            frame.reset();
            for (auto* view : views)
            {
                if (view)
                {
                    device.core.DestroyDescriptor(view);
                }
            }
        }

        static std::expected<std::unique_ptr<Fixture>, std::string>
        create(Device& device, const ShaderAsset& asset, const std::filesystem::path& file)
        {
            auto result = std::make_unique<Fixture>(device);
            for (size_t i = 0; i < result->outputs.size(); ++i)
            {
                auto buffer = Buffer::create(
                    device,
                    {64, 16, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
                if (!buffer)
                {
                    return std::unexpected(buffer.error());
                }
                result->outputs[i] = std::move(*buffer);
                const VriBufferViewDesc desc {result->outputs[i]->handle,
                                              VriDescriptorType_StorageBuffer,
                                              VriFormat_Unknown,
                                              0,
                                              64};
                if (device.core.CreateBufferView(device.handle, &desc, &result->views[i]) != VriResult_Success)
                {
                    return std::unexpected("Create material conformance output view failed");
                }
            }
            auto frame = Frame::create(device);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            result->frame = std::move(*frame);
            if (auto saved = asset.saveExpected(file); !saved)
            {
                return std::unexpected(saved.error());
            }
            auto runtime = ShaderRuntime::create(device, file);
            if (!runtime)
            {
                return std::unexpected(runtime.error());
            }
            result->runtime = std::move(*runtime);
            return result;
        }

        std::expected<VriPipeline*, std::string> prepare(ShaderMaterial& material, bool audit)
        {
            const std::array resources {ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {views[0]}},
                                        ShaderResourceViews {"audit", VriDescriptorType_StorageBuffer, {views[1]}}};
            return material.prepareExpected("Evaluate", {}, std::span(resources).first(audit ? 2 : 1));
        }

        Result execute(ShaderMaterial& material, VriPipeline* pipeline, bool multiply, bool audit, uint32_t bias = 0)
        {
            auto commands = frame->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            for (size_t i = 0; i < (audit ? 2u : 1u); ++i)
            {
                if (auto transitioned = outputs[i]->transitionExpected(
                        *commands,
                        {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
                    !transitioned)
                {
                    return transitioned;
                }
            }
            if (auto bound = material.bindExpected(*commands, pipeline); !bound)
            {
                return bound;
            }
            const VriDispatchDesc dispatch {1, 1, 1};
            device.core.CmdDispatch(*commands, &dispatch);
            if (auto submitted = frame->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            constexpr std::array<uint32_t, 4> payload {2, 3, 5, 7};
            for (size_t i = 0; i < (audit ? 2u : 1u); ++i)
            {
                auto data = readbackBufferExpected(device, *outputs[i]);
                if (!data || data->bytes.size() != 64)
                {
                    return std::unexpected(data ? "Wrong material conformance readback size" : data.error());
                }
                std::array<float, 16> actual;
                std::memcpy(actual.data(), data->bytes.data(), sizeof(actual));
                for (uint32_t thread = 0; thread < 4; ++thread)
                {
                    for (uint32_t lane = 0; lane < 4; ++lane)
                    {
                        const uint32_t input = payload[lane] + thread;
                        const auto     expected =
                            float((multiply ? input * (lane + 2) : input + lane + 1) + 7 + bias + i * 100);
                        if (actual[thread * 4 + lane] != expected)
                        {
                            return std::unexpected("Material conformance GPU mismatch at output/channel " +
                                                   std::to_string(i) + "/" + std::to_string(thread * 4 + lane));
                        }
                        ++channels;
                    }
                }
            }
            return {};
        }
    };

    Result runGpu(Device& device, const std::array<ShaderAsset, 3>& assets, const std::filesystem::path& root)
    {
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error)
        {
            return std::unexpected(error.message());
        }
        const auto file    = root / "runtime.vshaderc";
        auto       fixture = Fixture::create(device, assets[0], file);
        if (!fixture)
        {
            return std::unexpected(fixture.error());
        }
        auto&            gpu   = **fixture;
        auto             first = gpu.runtime->addMaterialExpected();
        MaterialInstance secondInstance;
        secondInstance.variant = "Swapped";
        auto second            = gpu.runtime->addMaterialExpected(std::move(secondInstance));
        if (!first || !second)
        {
            return std::unexpected("Create material conformance instances failed");
        }
        auto firstMaterial  = gpu.runtime->materialExpected(*first);
        auto secondMaterial = gpu.runtime->materialExpected(*second);
        auto instance       = gpu.runtime->instanceExpected(*first);
        if (!firstMaterial || !secondMaterial || !instance)
        {
            return std::unexpected("Read material conformance instances failed");
        }
        auto pipeline = gpu.prepare(**firstMaterial, false);
        if (!pipeline || !gpu.execute(**firstMaterial, *pipeline, false, false))
        {
            return std::unexpected("Initial conformance material output failed");
        }
        const auto originalPipeline = *pipeline;
        if (!(*instance)->setExpected(gpu.runtime->asset(), "model", int32_t(11)))
        {
            return std::unexpected("Edit material model ID failed");
        }
        pipeline = gpu.prepare(**firstMaterial, false);
        if (!pipeline || *pipeline != originalPipeline || !gpu.execute(**firstMaterial, *pipeline, true, false))
        {
            return std::unexpected("Material model uniform edit recreated code or dispatched incorrectly");
        }
        if (!(*instance)->setExpected(gpu.runtime->asset(), "model", int32_t(3)) ||
            !gpu.prepare(**firstMaterial, false) || !(*instance)->setVariantExpected(gpu.runtime->asset(), "Swapped"))
        {
            return std::unexpected("Prepare material variant switch failed");
        }
        const auto count    = (*firstMaterial)->pipelineCount();
        auto       rejected = gpu.prepare(**firstMaterial, false);
        if (rejected || rejected.error().find("audit") == std::string::npos ||
            (*firstMaterial)->pipelineCount() != count || !gpu.execute(**firstMaterial, originalPipeline, false, false))
        {
            return std::unexpected("Missing variant resource replaced the previously prepared material");
        }
        pipeline = gpu.prepare(**firstMaterial, true);
        if (!pipeline || !gpu.execute(**firstMaterial, *pipeline, true, true))
        {
            return std::unexpected("Swapped material variant failed resource/dispatch recovery");
        }
        if (!(*instance)->setVariantExpected(gpu.runtime->asset(), "OnlyAdd"))
        {
            return std::unexpected("Select single-model material variant failed");
        }
        pipeline = gpu.prepare(**firstMaterial, false);
        if (!pipeline || !gpu.execute(**firstMaterial, *pipeline, false, false) ||
            !(*instance)->setVariantExpected(gpu.runtime->asset(), "Default"))
        {
            return std::unexpected("Single-model material variant failed");
        }
        const auto prepare = [&](uint32_t index, ShaderMaterial& material) -> Result
        {
            auto prepared = gpu.prepare(material, index == *second);
            if (!prepared)
            {
                return std::unexpected(prepared.error());
            }
            return {};
        };
        if (!prepare(*first, **firstMaterial) || !prepare(*second, **secondMaterial))
        {
            return std::unexpected("Prepare material pair before reload failed");
        }
        const auto generation = gpu.runtime->generation();
        const auto retained   = [&]() -> Result
        {
            auto a = gpu.runtime->materialExpected(*first);
            auto b = gpu.runtime->materialExpected(*second);
            if (!a || !b || *a != *firstMaterial || *b != *secondMaterial || gpu.runtime->generation() != generation)
            {
                return std::unexpected("Rejected material conformance publication changed live owners");
            }
            if (auto checked = checkMetadata(gpu.runtime->asset()); !checked)
            {
                return checked;
            }
            if (auto checked = gpu.execute(**a, originalPipeline, false, false); !checked)
            {
                return checked;
            }
            auto prepared = gpu.prepare(**b, true);
            if (!prepared)
            {
                return std::unexpected(prepared.error());
            }
            return gpu.execute(**b, *prepared, true, true);
        };
        const std::array<std::byte, 4> corrupt {};
        if (!writeFileAtomicallyExpected(file, corrupt) || gpu.runtime->reloadExpected(prepare) || !retained() ||
            !assets[1].saveExpected(file))
        {
            return std::unexpected("Malformed conformance archive changed accepted runtime state");
        }
        auto missing = gpu.runtime->reloadExpected(
            [&](uint32_t, ShaderMaterial& material) -> Result
            {
                auto result = gpu.prepare(material, false);
                return result ? Result {} : std::unexpected(result.error());
            });
        if (missing || missing.error().find("audit") == std::string::npos || !retained())
        {
            return std::unexpected("Late variant-resource failure partially published the material pair");
        }
        {
            NativeFault fault(device);
            auto        failed = gpu.runtime->reloadExpected(
                [&](uint32_t index, ShaderMaterial& material) -> Result
                {
                    fault.reject = index == *second;
                    auto result  = prepare(index, material);
                    fault.reject = false;
                    return result;
                });
            if (failed || !fault.live.empty() || !retained())
            {
                return std::unexpected("Partial native failure changed live materials or leaked candidate pipelines");
            }
        }
        const auto checkPair = [&](bool remapped, uint32_t bias) -> Result
        {
            if (auto metadata = checkMetadata(gpu.runtime->asset(), remapped); !metadata)
            {
                return metadata;
            }
            for (uint32_t index : {*first, *second})
            {
                auto material = gpu.runtime->materialExpected(index);
                if (!material)
                {
                    return std::unexpected(material.error());
                }
                auto prepared = gpu.prepare(**material, index == *second);
                if (!prepared)
                {
                    return std::unexpected(prepared.error());
                }
                if (auto checked =
                        gpu.execute(**material, *prepared, remapped || index == *second, index == *second, bias);
                    !checked)
                {
                    return checked;
                }
            }
            return {};
        };
        auto published = gpu.runtime->reloadExpected(prepare);
        if (!published || !*published || gpu.runtime->generation() != generation + 1 || !checkPair(false, 4))
        {
            return std::unexpected("Material conformance candidate did not recover with edited code");
        }
        if (!assets[2].saveExpected(file))
        {
            return std::unexpected("Save remapped conformance candidate failed");
        }
        published = gpu.runtime->reloadExpected(prepare);
        if (!published || !*published || gpu.runtime->generation() != generation + 2 || !checkPair(true, 0) ||
            !assets[0].saveExpected(file))
        {
            return std::unexpected("Material conformance ID remap lost metadata or shader behavior");
        }
        published = gpu.runtime->reloadExpected(prepare);
        if (!published || !*published || gpu.runtime->generation() != generation + 3 || !checkPair(false, 0))
        {
            return std::unexpected("Material conformance restoration failed");
        }
        std::printf("MATERIAL_CONFORMANCE_GPU exact_channels=%llu variants=3 uniform_edit=1 layout_rejected=1 "
                    "reload_rejected=3 atomic_pair=1 remapped=1 restored=1\n",
                    static_cast<unsigned long long>(gpu.channels));
        return {};
    }

    Result run(int argc, char** argv)
    {
        std::filesystem::path      root;
        std::array<ShaderAsset, 3> assets;
        if (argc == 5)
        {
            root = argv[4];
            for (size_t i = 0; i < assets.size(); ++i)
            {
                auto asset = ShaderAsset::loadExpected(argv[i + 1]);
                if (!asset)
                {
                    return std::unexpected(asset.error());
                }
                assets[i] = std::move(*asset);
            }
        }
#ifdef VULTRA_WITH_GAME_SHADER_COMPILER
        else if (argc == 1 || argc == 2)
        {
            root          = argc == 2 ? std::filesystem::path(argv[1]) :
                                        std::filesystem::path("build/.tmp") /
                                   ("material-conformances-" +
                                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            auto compiled = compileAssets(root);
            if (!compiled)
            {
                return std::unexpected(compiled.error());
            }
            assets = std::move(*compiled);
        }
#endif
        else
        {
            return std::unexpected("Expected three cooked material archives and a writable root, or a fresh host root");
        }
        for (size_t i = 0; i < assets.size(); ++i)
        {
            if (auto checked = checkMetadata(assets[i], i == 2); !checked)
            {
                return checked;
            }
        }
        auto device = Device::create(true, nullptr, 0, true);
        if (!device)
        {
            return std::unexpected(device.error());
        }
        return runGpu(**device, assets, root / "run");
    }
} // namespace

int main(int argc, char** argv)
{
    auto result = run(argc, argv);
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
