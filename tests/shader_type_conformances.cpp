#include <vultra/drivers/rhi/buffer_readback.hpp>
#include <vultra/drivers/rhi/shader_bindings.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>

#ifdef VULTRA_WITH_SHADER_COMPILER
#include <vultra/drivers/rhi/shader_compiler.hpp>
#include <vultra/platform/os/file.hpp>
#endif

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;
    const std::array                             entries {ShaderEntry {"main", VriShaderStage_Compute}};
    const std::array                             uniforms {std::string_view("parameters")};
    const std::array<std::array<uint32_t, 4>, 4> tags {
        {{3, 11, 3, 11}, {3, 11, 3, 11}, {3, 3, 3, 3}, {0, UINT32_MAX, 0, UINT32_MAX}}};
    const std::array<std::array<bool, 4>, 4> multiplied {{{false, true, false, true},
                                                          {true, false, true, false},
                                                          {false, false, false, false},
                                                          {false, true, false, true}}};

    struct Fixture
    {
        Device&                           device;
        std::unique_ptr<Buffer>           output;
        VriDescriptor*                    view = nullptr;
        std::unique_ptr<ShaderBindings>   bindings;
        std::unique_ptr<ShaderPipeline>   pipeline;
        std::unique_ptr<Frame>            frame;
        std::optional<ShaderUniformBlock> values;

        explicit Fixture(Device& device) :
            device(device)
        {
        }

        ~Fixture()
        {
            // All dispatches complete before return; descriptor users still die before the view.
            frame.reset();
            pipeline.reset();
            bindings.reset();
            if (view)
            {
                device.core.DestroyDescriptor(view);
            }
        }

        static std::expected<std::unique_ptr<Fixture>, std::string> create(Device& device, const ShaderProgram& program)
        {
            auto result = std::make_unique<Fixture>(device);
            auto output = Buffer::create(
                device,
                {64, 16, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
            if (!output)
            {
                return std::unexpected(output.error());
            }
            result->output = std::move(*output);
            const VriBufferViewDesc desc {result->output->handle,
                                          VriDescriptorType_StorageBuffer,
                                          VriFormat_Unknown,
                                          0,
                                          64};
            if (device.core.CreateBufferView(device.handle, &desc, &result->view) != VriResult_Success || !result->view)
            {
                return std::unexpected("Create conformance output view failed");
            }
            auto bindings = ShaderBindings::create(device, program, entries, uniforms);
            if (!bindings)
            {
                return std::unexpected(bindings.error());
            }
            result->bindings   = std::move(*bindings);
            const auto* layout = program.parameters.field(uniforms[0]);
            if (!layout)
            {
                return std::unexpected("Missing conformance uniform layout");
            }
            auto values = ShaderUniformBlock::create(*layout);
            if (!values)
            {
                return std::unexpected(values.error());
            }
            result->values = std::move(*values);
            auto frame     = Frame::create(device);
            if (!frame)
            {
                return std::unexpected(frame.error());
            }
            result->frame = std::move(*frame);
            return result;
        }

        std::expected<VriPipeline*, std::string> build(const ShaderProgram&           program,
                                                       std::span<const VriShaderDesc> shaders) const
        {
            if (auto valid = bindings->validateLayoutExpected(program, entries); !valid)
            {
                return std::unexpected(valid.error());
            }
            VriPipeline*                 handle = nullptr;
            const VriComputePipelineDesc desc {bindings->layout(), shaders.front(), device.pipelineCache};
            const auto                   status = device.core.CreateComputePipeline(device.handle, &desc, &handle);
            if (status != VriResult_Success || !handle)
            {
                if (handle)
                {
                    device.core.DestroyPipeline(handle);
                }
                return std::unexpected("Create conformance compute pipeline failed");
            }
            return handle;
        }

        Result execute(const std::array<uint32_t, 4>& dispatchTags,
                       const std::array<bool, 4>&     multiply,
                       uint32_t                       addOffset = 0)
        {
            const std::array<uint32_t, 4> payload {2, 3, 5, 7};
            const std::array              writes {
                ShaderUniformWrite {"tags", std::span(dispatchTags), ShaderParameterKind::eVector, 1, 4},
                ShaderUniformWrite {"value", std::span(payload), ShaderParameterKind::eVector, 1, 4}};
            if (auto edited = values->setExpected(writes); !edited)
            {
                return std::unexpected(edited.error());
            }
            const std::array resources {ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {view}}};
            const std::array uploads {ShaderUniformUpload {uniforms[0], *values}};
            if (auto updated = bindings->updateExpected(resources, uploads); !updated)
            {
                return updated;
            }
            auto commands = frame->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            auto uniform = bindings->uniformBufferExpected(uniforms[0]);
            if (!uniform)
            {
                return std::unexpected(uniform.error());
            }
            if (!(*uniform)->transitionExpected(*commands,
                                                {VriAccess_ConstantBufferRead, VriPipelineStage_ComputeShader}) ||
                !output->transitionExpected(*commands,
                                            {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader}))
            {
                return std::unexpected("Transition conformance resources failed");
            }
            if (auto bound = bindings->bindExpected(*commands, pipeline->handle()); !bound)
            {
                return bound;
            }
            const VriDispatchDesc dispatch {1, 1, 1};
            device.core.CmdDispatch(*commands, &dispatch);
            if (auto submitted = frame->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            auto readback = readbackBufferExpected(device, *output);
            if (!readback || readback->bytes.size() != 64)
            {
                return std::unexpected(readback ? "Wrong conformance readback size" : readback.error());
            }
            std::array<uint32_t, 16> actual;
            std::memcpy(actual.data(), readback->bytes.data(), sizeof(actual));
            for (uint32_t thread = 0; thread < 4; ++thread)
            {
                for (uint32_t lane = 0; lane < 4; ++lane)
                {
                    const auto value    = payload[lane] + thread;
                    const auto expected = (multiply[thread] ? value * (lane + 2) : value + lane + 1 + addOffset) + 7;
                    if (actual[thread * 4 + lane] != expected)
                    {
                        return std::unexpected(
                            "Conformance GPU mismatch at channel " + std::to_string(thread * 4 + lane) + ": " +
                            std::to_string(actual[thread * 4 + lane]) + " expected " + std::to_string(expected));
                    }
                }
            }
            return {};
        }
    };

#ifdef VULTRA_WITH_SHADER_COMPILER
    ShaderCompileOptions options(size_t selection)
    {
        ShaderCompileOptions result;
        result.entries.assign(entries.begin(), entries.end());
        result.linkModules = {"type_conformance_models.slang"};
        const std::array<uint32_t, 4> addIds {3, 11, 3, 0};
        const std::array<uint32_t, 4> multiplyIds {11, 3, 11, UINT32_MAX};
        result.typeConformances = {{"AddModel", "IMaterialModel", addIds[selection]}, {"AddModel", "IMarker", 3}};
        if (selection != 2)
        {
            result.typeConformances.push_back({"MultiplyModel", "IMaterialModel", multiplyIds[selection]});
        }
        return result;
    }

    std::expected<std::array<ShaderProgram, 4>, std::string> compilePrograms(const std::filesystem::path& root)
    {
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error)
        {
            return std::unexpected("Create conformance test directory: " + error.message());
        }
        for (const auto* file :
             {"type_conformances.slang", "type_conformance_types.slang", "type_conformance_models.slang"})
        {
            std::filesystem::copy_file(std::filesystem::path("tests/shaders") / file, root / file, error);
            if (error)
            {
                return std::unexpected("Copy conformance fixture: " + error.message());
            }
        }
        const auto file     = root / "type_conformances.slang";
        auto       compiler = ShaderCompiler::create(root / "cache");
        if (!compiler)
        {
            return std::unexpected(compiler.error());
        }
        std::array<ShaderProgram, 4> programs;
        for (size_t i = 0; i < programs.size(); ++i)
        {
            auto program = (*compiler)->compileExpected(file, options(i));
            if (!program)
            {
                return std::unexpected(program.error());
            }
            const auto statistics = (*compiler)->lastCompileStatistics();
            if (statistics.programCacheHit || (i != 0 && !statistics.moduleCacheHit))
            {
                return std::unexpected("Conformance selection did not isolate program keys and reuse frontend IR");
            }
            for (size_t previous = 0; previous < i; ++previous)
            {
                if (programs[previous].compileKey == program->compileKey)
                {
                    return std::unexpected("Distinct conformance selections share a program cache key");
                }
            }
            const auto cooked = root / ("selection-" + std::to_string(i) + ".vshaderc");
            if (auto saved = program->saveExpected(cooked); !saved)
            {
                return std::unexpected(saved.error());
            }
            auto loaded = ShaderProgram::loadExpected(cooked);
            if (!loaded || loaded->shaders.size() != 1 ||
                loaded->shaders.front().words != program->shaders.front().words ||
                loaded->compileKey != program->compileKey)
            {
                return std::unexpected("Conformance cook changed program identity");
            }
            programs[i] = std::move(*loaded);
        }
        auto reopened = ShaderCompiler::create(root / "cache");
        if (!reopened)
        {
            return std::unexpected(reopened.error());
        }
        for (size_t i = 0; i < programs.size(); ++i)
        {
            auto cached = (*reopened)->compileExpected(file, options(i));
            if (!cached || !(*reopened)->lastCompileStatistics().programCacheHit || cached->shaders.size() != 1 ||
                cached->shaders.front().words != programs[i].shaders.front().words ||
                cached->compileKey != programs[i].compileKey)
            {
                return std::unexpected("Reopened conformance cache lost selected code or identity");
            }
        }
        const auto                                      base = options(0);
        std::vector<std::vector<ShaderTypeConformance>> invalid {
            {{"", "IMaterialModel", 3}},
            {{"AddModel", "", 3}},
            {{std::string("Add\0Model", 9), "IMaterialModel", 3}},
            {{"AddModel", std::string("IMaterial\0Model", 15), 3}},
            {{"AddModel", "IMaterialModel", 3}, {"AddModel", "IMaterialModel", 11}},
            {{"AddModel", "IMaterialModel", 3}, {"MultiplyModel", "IMaterialModel", 3}},
            {{"MissingModel", "IMaterialModel", 3}},
            {{"AddModel", "MissingInterface", 3}},
            {{"PlainPayload", "IMaterialModel", 3}},
            {{"AddModel", "PlainPayload", 3}},
            {{"AddModel", "IMaterialModel", 3}, {"AddAlias", "IMaterialModel", 11}},
            {{"AddModel", "IMaterialModel", 3}, {"MultiplyModel", "MaterialInterfaceAlias", 3}}};
        const std::array<std::string_view, 12> reasons {"requires nonempty",
                                                        "requires nonempty",
                                                        "without NULs",
                                                        "without NULs",
                                                        "Duplicate implementation or dispatch ID",
                                                        "Duplicate implementation or dispatch ID",
                                                        "Unknown shader type/interface",
                                                        "Unknown shader type/interface",
                                                        "Create shader type conformance",
                                                        "not an interface",
                                                        "Duplicate resolved implementation or dispatch ID",
                                                        "Duplicate resolved implementation or dispatch ID"};
        for (size_t i = 0; i < invalid.size(); ++i)
        {
            auto request             = base;
            request.typeConformances = invalid[i];
            const auto rejected      = (*compiler)->compileExpected(file, request);
            if (rejected || rejected.error().find(reasons[i]) == std::string::npos)
            {
                return std::unexpected("Conformance rejection reason differs at case " + std::to_string(i) + ": " +
                                       (rejected ? "accepted" : rejected.error()));
            }
            auto recovered = (*compiler)->compileExpected(file, base);
            if (!recovered || !(*compiler)->lastCompileStatistics().programCacheHit || recovered->shaders.size() != 1 ||
                recovered->shaders.front().words != programs[0].shaders.front().words)
            {
                return std::unexpected("Failed conformance poisoned the valid cached program");
            }
        }
        std::printf(
            "TYPE_CONFORMANCE_COMPILER selections=4 reopened_hits=4 frontend_reuse=3 rejected=%zu recovered=%zu\n",
            invalid.size(),
            invalid.size());
        return programs;
    }

    struct PipelineFault
    {
        Device&                                             device;
        decltype(VriCoreInterface {}.CreateComputePipeline) original;
        inline static PipelineFault*                        s_Active = nullptr;

        explicit PipelineFault(Device& device) :
            device(device),
            original(device.core.CreateComputePipeline)
        {
            s_Active                          = this;
            device.core.CreateComputePipeline = create;
        }

        ~PipelineFault()
        {
            device.core.CreateComputePipeline = original;
            s_Active                          = nullptr;
        }

        static VriResult VRI_CALL create(VriDevice* device, const VriComputePipelineDesc* desc, VriPipeline** handle)
        {
            const auto result = s_Active->original(device, desc, handle);
            return result == VriResult_Success ? VriResult_OutOfMemory : result;
        }
    };

    Result reload(Device& device, const ShaderProgram& program, const std::filesystem::path& root)
    {
        auto fixture = Fixture::create(device, program);
        if (!fixture)
        {
            return std::unexpected(fixture.error());
        }
        auto& gpu      = **fixture;
        auto  pipeline = ShaderPipeline::create(
            device,
            root / "type_conformances.slang",
            options(0),
            [&](const ShaderProgram& candidate, std::span<const VriShaderDesc> shaders)
            {
                return gpu.build(candidate, shaders);
            },
            root,
            root / "cache");
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        gpu.pipeline = std::move(*pipeline);
        if (auto checked = gpu.execute(tags[0], multiplied[0]); !checked)
        {
            return checked;
        }
        const auto    identity   = gpu.pipeline->identity();
        const auto    generation = gpu.pipeline->generation();
        const auto    handle     = gpu.pipeline->handle();
        const auto    models     = root / "type_conformance_models.slang";
        std::ifstream input(models, std::ios::binary);
        if (!input)
        {
            return std::unexpected("Read conformance models for reload");
        }
        const std::string original {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad())
        {
            return std::unexpected("Read conformance models failed");
        }
        input.close();
        const auto write = [&](std::string_view source) -> Result
        {
            return writeFileAtomicallyExpected(models, std::as_bytes(std::span(source)));
        };
        if (auto written = write("invalid Slang source"); !written)
        {
            return written;
        }
        if (gpu.pipeline->reloadExpected() || gpu.pipeline->handle() != handle ||
            gpu.pipeline->generation() != generation ||
            gpu.pipeline->identity().bytecodeHash != identity.bytecodeHash ||
            gpu.pipeline->identity().compileKey != identity.compileKey)
        {
            return std::unexpected("Failed conformance source reload replaced accepted code");
        }
        if (auto checked = gpu.execute(tags[0], multiplied[0]); !checked)
        {
            return checked;
        }
        auto       changed = original;
        const auto offset  = changed.find("uint4(1, 2, 3, 4)");
        if (offset == std::string::npos)
        {
            return std::unexpected("Missing conformance reload expression");
        }
        changed.replace(offset, std::strlen("uint4(1, 2, 3, 4)"), "uint4(5, 6, 7, 8)");
        if (auto written = write(changed); !written)
        {
            return written;
        }
        {
            PipelineFault fault(device);
            if (gpu.pipeline->reloadExpected() || gpu.pipeline->handle() != handle ||
                gpu.pipeline->generation() != generation || gpu.pipeline->identity().compileKey != identity.compileKey)
            {
                return std::unexpected("Failed conformance native build replaced accepted code");
            }
        }
        if (auto checked = gpu.execute(tags[0], multiplied[0]); !checked)
        {
            return checked;
        }
        if (auto replaced = gpu.pipeline->reloadExpected(); !replaced)
        {
            return replaced;
        }
        if (gpu.pipeline->generation() != generation + 1 ||
            gpu.pipeline->identity().compileKey == identity.compileKey ||
            gpu.pipeline->identity().bytecodeHash == identity.bytecodeHash)
        {
            return std::unexpected("Conformance dependency edit did not change accepted program identity");
        }
        if (auto checked = gpu.execute(tags[0], multiplied[0], 4); !checked)
        {
            return checked;
        }
        if (auto written = write(original); !written)
        {
            return written;
        }
        if (auto restored = gpu.pipeline->reloadExpected(); !restored)
        {
            return restored;
        }
        if (gpu.pipeline->generation() != generation + 2 ||
            gpu.pipeline->identity().compileKey != identity.compileKey ||
            gpu.pipeline->identity().bytecodeHash != identity.bytecodeHash)
        {
            return std::unexpected("Conformance source recovery did not restore accepted identity");
        }
        if (auto checked = gpu.execute(tags[0], multiplied[0]); !checked)
        {
            return checked;
        }
        std::puts("TYPE_CONFORMANCE_RELOAD source_rejected=1 native_rejected=1 retained=1 recovered=1");
        return {};
    }
#endif

    Result run(int argc, char** argv)
    {
        std::array<ShaderProgram, 4> programs;
        std::filesystem::path        root;
        if (argc == 5)
        {
            for (size_t i = 0; i < programs.size(); ++i)
            {
                auto program = ShaderProgram::loadExpected(argv[i + 1]);
                if (!program)
                {
                    return std::unexpected(program.error());
                }
                programs[i] = std::move(*program);
            }
        }
#ifdef VULTRA_WITH_SHADER_COMPILER
        else if (argc == 1 || argc == 2)
        {
            root          = argc == 2 ? std::filesystem::path(argv[1]) :
                                        std::filesystem::path("build/.tmp") /
                                   ("type-conformances-" +
                                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            auto compiled = compilePrograms(root);
            if (!compiled)
            {
                return std::unexpected(compiled.error());
            }
            programs = std::move(*compiled);
        }
#endif
        else
        {
            return std::unexpected("Expected four cooked conformance programs or a fresh host output directory");
        }
        auto device = Device::create(true, nullptr, 0, true);
        if (!device)
        {
            return std::unexpected(device.error());
        }
        for (size_t i = 0; i < programs.size(); ++i)
        {
            auto fixture = Fixture::create(**device, programs[i]);
            if (!fixture)
            {
                return std::unexpected(fixture.error());
            }
            auto& gpu      = **fixture;
            auto  pipeline = ShaderPipeline::create(**device,
                                                   programs[i],
                                                    {entries.begin(), entries.end()},
                                                   [&](std::span<const VriShaderDesc> shaders)
                                                   {
                                                       return gpu.build(programs[i], shaders);
                                                   });
            if (!pipeline)
            {
                return std::unexpected(pipeline.error());
            }
            gpu.pipeline = std::move(*pipeline);
            if (auto checked = gpu.execute(tags[i], multiplied[i]); !checked)
            {
                return checked;
            }
            if (i == 0)
            {
                const std::array<uint32_t, 4> edited {11, 3, 11, 3};
                if (auto checked = gpu.execute(edited, multiplied[1]); !checked)
                {
                    return checked;
                }
            }
        }
#ifdef VULTRA_WITH_SHADER_COMPILER
        if (!root.empty())
        {
            if (auto checked = reload(**device, programs[0], root); !checked)
            {
                return checked;
            }
        }
#endif
        std::puts("TYPE_CONFORMANCE_GPU exact_channels=80 selections=4 uniform_edit=1 sparse_ids=1 "
                  "uint32_limits=1 interface_scopes=2 cooked=1");
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    const auto result = run(argc, argv);
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
