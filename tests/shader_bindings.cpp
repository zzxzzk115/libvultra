#include <vultra/core/base/stable_id.hpp>
#include <vultra/drivers/rhi/buffer_readback.hpp>
#include <vultra/drivers/rhi/shader_bindings.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/drivers/rhi/shader_uniform_block.hpp>

#ifdef VULTRA_WITH_SHADER_COMPILER
#include <vultra/drivers/rhi/shader_compiler.hpp>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <set>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;
    const std::array entries {ShaderEntry {"main", VriShaderStage_Compute}};

    struct Faults
    {
        Device&                      device;
        VriCoreInterface             original;
        std::set<VriPipelineLayout*> layouts;
        std::set<VriDescriptorPool*> pools;
        unsigned                     failure       = 0;
        unsigned                     updates       = 0;
        unsigned                     creations     = 0;
        uint32_t                     allocatedSets = 0;
        inline static Faults*        s_Active      = nullptr;

        explicit Faults(Device& device) :
            device(device),
            original(device.core)
        {
            s_Active                           = this;
            device.core.CreatePipelineLayout   = layout;
            device.core.DestroyPipelineLayout  = destroyLayout;
            device.core.CreateDescriptorPool   = pool;
            device.core.DestroyDescriptorPool  = destroyPool;
            device.core.AllocateDescriptorSets = allocate;
            device.core.UpdateDescriptorRanges = update;
        }

        ~Faults()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static VriResult VRI_CALL layout(VriDevice*                   device,
                                         const VriPipelineLayoutDesc* desc,
                                         VriPipelineLayout**          output)
        {
            auto& self = *s_Active;
            ++self.creations;
            const auto result = self.original.CreatePipelineLayout(device, desc, output);
            if (*output)
            {
                self.layouts.insert(*output);
            }
            return result == VriResult_Success && self.failure == 1 ? VriResult_OutOfMemory : result;
        }

        static void VRI_CALL destroyLayout(VriPipelineLayout* layout)
        {
            s_Active->layouts.erase(layout);
            s_Active->original.DestroyPipelineLayout(layout);
        }

        static VriResult VRI_CALL pool(VriDevice* device, const VriDescriptorPoolDesc* desc, VriDescriptorPool** output)
        {
            auto& self = *s_Active;
            ++self.creations;
            const auto result = self.original.CreateDescriptorPool(device, desc, output);
            if (*output)
            {
                self.pools.insert(*output);
            }
            return result == VriResult_Success && self.failure == 2 ? VriResult_OutOfMemory : result;
        }

        static void VRI_CALL destroyPool(VriDescriptorPool* pool)
        {
            s_Active->pools.erase(pool);
            s_Active->original.DestroyDescriptorPool(pool);
        }

        static VriResult VRI_CALL allocate(VriDescriptorPool*       pool,
                                           const VriPipelineLayout* layout,
                                           uint32_t                 index,
                                           VriDescriptorSet**       output,
                                           uint32_t                 count)
        {
            auto& self = *s_Active;
            ++self.creations;
            const auto result = self.original.AllocateDescriptorSets(pool, layout, index, output, count);
            self.allocatedSets |= 1u << index;
            return result == VriResult_Success && self.failure == 3 && index == 2 ? VriResult_OutOfMemory : result;
        }

        static void VRI_CALL update(VriDescriptorSet*                   set,
                                    uint32_t                            base,
                                    uint32_t                            count,
                                    const VriDescriptorRangeUpdateDesc* ranges)
        {
            s_Active->updates += count;
            s_Active->original.UpdateDescriptorRanges(set, base, count, ranges);
        }
    };

    struct Views
    {
        Device&                     device;
        std::vector<VriDescriptor*> owned;

        ~Views()
        {
            for (auto* view : owned)
            {
                device.core.DestroyDescriptor(view);
            }
        }

        std::expected<VriDescriptor*, std::string> create(const VriBufferViewDesc& desc)
        {
            VriDescriptor* view   = nullptr;
            const auto     result = device.core.CreateBufferView(device.handle, &desc, &view);
            if (view)
            {
                owned.push_back(view);
            }
            if (result != VriResult_Success || !view)
            {
                return std::unexpected("Create shader-binding fixture view failed");
            }
            return view;
        }
    };

    struct Prepared
    {
        // Pipeline expires before its borrowed layout.
        std::unique_ptr<ShaderBindings> bindings;
        std::unique_ptr<ShaderPipeline> pipeline;
    };

    std::expected<VriPipeline*, std::string>
    buildPipeline(Device& device, VriPipelineLayout* layout, std::span<const VriShaderDesc> shaders)
    {
        const VriComputePipelineDesc desc {layout, shaders.front(), device.pipelineCache};
        VriPipeline*                 result = nullptr;
        const auto                   status = device.core.CreateComputePipeline(device.handle, &desc, &result);
        if (status != VriResult_Success || !result)
        {
            if (result)
            {
                device.core.DestroyPipeline(result);
            }
            return std::unexpected("Create direct reflected compute pipeline failed");
        }
        return result;
    }

    std::expected<std::unique_ptr<Prepared>, std::string> prepare(Device& device, const ShaderProgram& program)
    {
        auto bindings = ShaderBindings::create(device, program, entries);
        if (!bindings)
        {
            return std::unexpected(bindings.error());
        }
        auto prepared      = std::make_unique<Prepared>();
        prepared->bindings = std::move(*bindings);
        auto pipeline      = ShaderPipeline::create(
            device,
            program,
            std::vector<ShaderEntry>(entries.begin(), entries.end()),
            [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                return buildPipeline(device, prepared->bindings->layout(), shaders);
            });
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        prepared->pipeline = std::move(*pipeline);
        return prepared;
    }

    Result run(Device&                      device,
               const ShaderProgram&         program,
               const ShaderProgram&         replacement,
               const ShaderProgram&         compatible,
               const ShaderProgram&         changedType,
               const std::filesystem::path& reloadFile)
    {
        const auto* layout = program.parameters.field("parameters");
        if (!layout)
        {
            return std::unexpected("Missing reflected uniform fixture");
        }
        auto values = ShaderUniformBlock::create(*layout);
        if (!values)
        {
            return std::unexpected(values.error());
        }
        const std::array multiplier {uint32_t(2)};
        const std::array bias {int32_t(-3)};
        const std::array enabled {true};
        const std::array gain {1.f};
        const std::array writes {ShaderUniformWrite {"multiplier", std::span(multiplier)},
                                 ShaderUniformWrite {"bias", std::span(bias)},
                                 ShaderUniformWrite {"enabled", std::span(enabled)},
                                 ShaderUniformWrite {"gain", std::span(gain)}};
        if (auto edited = values->setExpected(writes); !edited)
        {
            return std::unexpected(edited.error());
        }
        auto input = Buffer::create(device, {64, 4, VriBufferUsage_StorageBuffer, VriMemoryLocation_HostUpload});
        auto uniform =
            Buffer::create(device,
                           {values->data().size(), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
        auto output =
            Buffer::create(device,
                           {4, 4, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
        if (!input || !uniform || !output)
        {
            return std::unexpected("Create shader-binding fixture buffers failed");
        }
        auto* mapped = static_cast<std::byte*>(device.core.MapBuffer((*input)->handle, 0, 64));
        if (!mapped)
        {
            return std::unexpected("Map shader-binding fixture input failed");
        }
        const std::array<uint32_t, 3> data {7, 29, 11};
        std::memset(mapped, 0, 64);
        for (size_t i = 0; i < data.size(); ++i)
        {
            std::memcpy(mapped + i * 16, &data[i], 4);
        }
        device.core.UnmapBuffer((*input)->handle);
        const auto upload = [&]() -> Result
        {
            auto* destination = device.core.MapBuffer((*uniform)->handle, 0, values->data().size());
            if (!destination)
            {
                return std::unexpected("Map reflected uniform upload failed");
            }
            std::memcpy(destination, values->data().data(), values->data().size());
            device.core.UnmapBuffer((*uniform)->handle);
            return {};
        };
        if (auto uploaded = upload(); !uploaded)
        {
            return uploaded;
        }
        Views                         views {device, {}};
        std::array<VriDescriptor*, 5> descriptors {};
        for (size_t i = 0; i < 3; ++i)
        {
            auto view =
                views.create({(*input)->handle, VriDescriptorType_StructuredBuffer, VriFormat_Unknown, i * 16, 4});
            if (!view)
            {
                return std::unexpected(view.error());
            }
            descriptors[i] = *view;
        }
        auto constant = views.create(
            {(*uniform)->handle, VriDescriptorType_ConstantBuffer, VriFormat_Unknown, 0, values->data().size()});
        auto storage = views.create({(*output)->handle, VriDescriptorType_StorageBuffer, VriFormat_Unknown, 0, 4});
        if (!constant || !storage)
        {
            return std::unexpected("Create reflected fixture uniform/output views failed");
        }
        descriptors[3] = *constant;
        descriptors[4] = *storage;
        Faults faults(device);
        for (unsigned stage = 1; stage <= 3; ++stage)
        {
            faults.failure = stage;
            auto rejected  = ShaderBindings::create(device, program, entries);
            faults.failure = 0;
            if (rejected || !faults.layouts.empty() || !faults.pools.empty())
            {
                return std::unexpected("Failed reflected binding creation leaked native owners");
            }
        }
        auto current = prepare(device, program);
        if (!current)
        {
            return std::unexpected(current.error());
        }
        if ((*current)->bindings->pushConstantSize() != 4 || faults.allocatedSets != 15)
        {
            return std::unexpected("Reflected push constants or sparse positional sets were lost");
        }
        const auto reflected = (*current)->bindings->bindings();
        const auto single    = std::ranges::find(reflected, "single", &ShaderResourceBinding::name);
        const auto inputs    = std::ranges::find(reflected, "inputs", &ShaderResourceBinding::name);
        if (single == reflected.end() || !single->array || single->count != 1 || inputs == reflected.end() ||
            !inputs->array || inputs->count != 2)
        {
            return std::unexpected("Reflected capacity-one array addressing was lost");
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
        if ((*current)->bindings->bindExpected(*commands, (*current)->pipeline->handle()))
        {
            return std::unexpected("Unprepared reflected resources accepted command binding");
        }
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return discarded;
        }
        std::array resources {
            ShaderResourceViews {"single", VriDescriptorType_StructuredBuffer, {descriptors[0]}},
            ShaderResourceViews {"inputs", VriDescriptorType_StructuredBuffer, {descriptors[0], descriptors[1]}},
            ShaderResourceViews {"parameters", VriDescriptorType_ConstantBuffer, {descriptors[3]}},
            ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {descriptors[4]}}};
        const auto creations = faults.creations;
        if (auto accepted = (*current)->bindings->updateExpected(resources); !accepted)
        {
            return accepted;
        }
        const auto refreshed = faults.updates;
        if (!(*current)->bindings->updateExpected(resources) || faults.updates != refreshed + resources.size())
        {
            return std::unexpected("Identical CPU descriptor addresses were not refreshed");
        }
        const auto execute = [&](uint32_t expected) -> Result
        {
            auto command = (*frame)->beginExpected();
            if (!command)
            {
                return std::unexpected(command.error());
            }
            if (!(*input)->transitionExpected(*command,
                                              {VriAccess_ShaderResourceRead, VriPipelineStage_ComputeShader}) ||
                !(*uniform)->transitionExpected(*command,
                                                {VriAccess_ConstantBufferRead, VriPipelineStage_ComputeShader}) ||
                !(*output)->transitionExpected(*command,
                                               {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader}))
            {
                return std::unexpected("Transition reflected shader-binding fixture failed");
            }
            if (auto bound = (*current)->bindings->bindExpected(*command, (*current)->pipeline->handle()); !bound)
            {
                return bound;
            }
            const uint32_t increment = 5;
            device.core.CmdSetConstants(*command, 0, &increment, 4);
            const VriDispatchDesc dispatch {1, 1, 1};
            device.core.CmdDispatch(*command, &dispatch);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            auto bytes = readbackBufferExpected(device, **output);
            if (!bytes || bytes->bytes.size() != 4)
            {
                return std::unexpected("Read reflected shader-binding output failed");
            }
            uint32_t actual;
            std::memcpy(&actual, bytes->bytes.data(), 4);
            if (actual != expected)
            {
                return std::unexpected("Reflected shader-binding GPU value differs: " + std::to_string(actual));
            }
            return {};
        };
        if (auto checked = execute(88); !checked)
        {
            return checked;
        }
        for (unsigned test = 0; test < 5; ++test)
        {
            auto invalid        = resources;
            invalid[0].views[0] = descriptors[2];
            switch (test)
            {
                case 0:
                    invalid[3].type = VriDescriptorType_Texture;
                    break;
                case 1:
                    invalid[3].views[0] = nullptr;
                    break;
                case 2:
                    invalid[1].views.pop_back();
                    break;
                case 3:
                    invalid[3].name = "unknown";
                    break;
                case 4:
                    invalid[3].name = invalid[2].name;
                    break;
            }
            const auto updates = faults.updates;
            if ((*current)->bindings->updateExpected(invalid) || faults.updates != updates)
            {
                return std::unexpected("Rejected mixed binding batch changed accepted descriptors");
            }
        }
        if (auto checked = execute(88); !checked)
        {
            return checked;
        }
        if (faults.creations != creations)
        {
            return std::unexpected("Resource updates or binds recreated native layouts/sets");
        }
        if (auto saved = program.saveExpected(reloadFile); !saved)
        {
            return saved;
        }
        auto*                retainedBindings = (*current)->bindings.get();
        unsigned             builds           = 0;
        ShaderCompileOptions options;
        options.entries.assign(entries.begin(), entries.end());
        auto live = ShaderPipeline::create(
            device,
            reloadFile,
            options,
            [retainedBindings, &device, &builds](const ShaderProgram& candidate, std::span<const VriShaderDesc> shaders)
                -> std::expected<VriPipeline*, std::string>
            {
                if (auto valid = retainedBindings->validateLayoutExpected(candidate, entries); !valid)
                {
                    return std::unexpected(valid.error());
                }
                ++builds;
                return buildPipeline(device, retainedBindings->layout(), shaders);
            });
        if (!live)
        {
            return std::unexpected(live.error());
        }
        (*current)->pipeline = std::move(*live);
        for (const auto* invalid : {&replacement, &changedType})
        {
            if (auto saved = invalid->saveExpected(reloadFile); !saved)
            {
                return saved;
            }
            auto&      pipeline     = *(*current)->pipeline;
            const auto handle       = pipeline.handle();
            const auto generation   = pipeline.generation();
            const auto identity     = pipeline.identity();
            const auto updates      = faults.updates;
            const auto beforeBuilds = builds;
            auto       rejected     = pipeline.reloadExpected();
            if (rejected || rejected.error().find("Reflected shader layout changed") == std::string::npos ||
                pipeline.handle() != handle || pipeline.generation() != generation ||
                pipeline.identity().compileKey != identity.compileKey ||
                pipeline.identity().bytecodeHash != identity.bytecodeHash || faults.updates != updates ||
                builds != beforeBuilds || faults.creations != creations)
            {
                return std::unexpected("Incompatible shader reload changed accepted pipeline/bindings");
            }
            if (auto checked = execute(88); !checked)
            {
                return checked;
            }
        }
        for (const auto* accepted : {&compatible, &program})
        {
            if (auto saved = accepted->saveExpected(reloadFile); !saved)
            {
                return saved;
            }
            auto&      pipeline     = *(*current)->pipeline;
            const auto handle       = pipeline.handle();
            const auto generation   = pipeline.generation();
            const auto hash         = pipeline.identity().bytecodeHash;
            const auto updates      = faults.updates;
            const auto beforeBuilds = builds;
            if (auto reloaded = pipeline.reloadExpected(); !reloaded)
            {
                return reloaded;
            }
            if (pipeline.handle() == handle || pipeline.generation() != generation + 1 ||
                pipeline.identity().bytecodeHash == hash || !pipeline.diagnostics().empty() ||
                faults.updates != updates || builds != beforeBuilds + 1 || faults.creations != creations)
            {
                return std::unexpected("Compatible reload did not preserve bindings or commit a new pipeline");
            }
            if (auto checked = execute(accepted == &compatible ? 90 : 88); !checked)
            {
                return checked;
            }
        }
        auto candidate = prepare(device, replacement);
        if (!candidate)
        {
            return std::unexpected(candidate.error());
        }
        const auto updates = faults.updates;
        if ((*candidate)->bindings->updateExpected(resources) || faults.updates != updates)
        {
            return std::unexpected("Changed shader layout accepted the preceding array count");
        }
        if (auto checked = execute(88); !checked)
        {
            return checked;
        }
        resources[1].views.push_back(descriptors[2]);
        if (auto accepted = (*candidate)->bindings->updateExpected(resources); !accepted)
        {
            return accepted;
        }
        current->swap(*candidate);
        candidate->reset();
        if (auto checked = execute(110); !checked)
        {
            return checked;
        }
        const auto       nativePipeline = (*current)->pipeline->handle();
        const auto       nativeLayout   = (*current)->bindings->layout();
        const std::array changedMultiplier {uint32_t(3)};
        const std::array edit {ShaderUniformWrite {"multiplier", std::span(changedMultiplier)}};
        if (auto changed = values->setExpected(edit); !changed || !*changed)
        {
            return std::unexpected("Reflected uniform edit did not change data");
        }
        if (auto uploaded = upload(); !uploaded)
        {
            return uploaded;
        }
        if (auto checked = execute(164); !checked)
        {
            return checked;
        }
        if ((*current)->pipeline->handle() != nativePipeline || (*current)->bindings->layout() != nativeLayout)
        {
            return std::unexpected("Reflected uniform update rebuilt accepted GPU state");
        }
        std::puts("SHADER_BINDINGS_GPU exact=88,110,164 array_one=1 sparse_sets=4 atomic_batch=1 refresh=1 "
                  "layout_candidate=1 storage_reuse=1 native_failures=3 reload_values=88,90,88 reload_reject=2");
        return {};
    }

    struct Fixtures
    {
        std::array<ShaderProgram, 4> programs;
        std::filesystem::path        reloadFile;
    };

    std::expected<Fixtures, std::string> fixturesExpected(int argc, char** argv)
    {
        Fixtures fixtures;
        if (argc == 6)
        {
            for (size_t i = 0; i < fixtures.programs.size(); ++i)
            {
                auto program = ShaderProgram::loadExpected(argv[i + 1]);
                if (!program)
                {
                    return std::unexpected(program.error());
                }
                fixtures.programs[i] = std::move(*program);
            }
            fixtures.reloadFile = argv[5];
            return fixtures;
        }
#ifdef VULTRA_WITH_SHADER_COMPILER
        if (argc == 1)
        {
            auto compiler = ShaderCompiler::create({});
            if (!compiler)
            {
                return std::unexpected(compiler.error());
            }
            const std::array<std::vector<ShaderDefine>, 4> defines {
                std::vector<ShaderDefine> {{"INPUT_COUNT", "2"}},
                std::vector<ShaderDefine> {{"INPUT_COUNT", "3"}},
                std::vector<ShaderDefine> {{"INPUT_COUNT", "2"}, {"OUTPUT_OFFSET", "2"}},
                std::vector<ShaderDefine> {{"INPUT_COUNT", "2"}, {"FLOAT_MULTIPLIER", "1"}}};
            ShaderCompileOptions options;
            options.entries.assign(entries.begin(), entries.end());
            for (size_t i = 0; i < fixtures.programs.size(); ++i)
            {
                options.defines = defines[i];
                auto program    = (*compiler)->compileExpected("tests/shaders/shader_bindings.slang", options);
                if (!program)
                {
                    return std::unexpected(program.error());
                }
                fixtures.programs[i] = std::move(*program);
            }
            fixtures.reloadFile = std::filesystem::path("build/.tmp/shader-bindings") /
                                  StableId::generate().toString() / "reload.vshaderc";
            std::error_code error;
            std::filesystem::create_directories(fixtures.reloadFile.parent_path(), error);
            if (error)
            {
                return std::unexpected("Create shader binding fixture directory: " + error.message());
            }
            return fixtures;
        }
#endif
        return std::unexpected(
            "Usage: test-shader-bindings bindings-2.vshaderc bindings-3.vshaderc compatible.vshaderc "
            "changed-type.vshaderc reload.vshaderc");
    }
} // namespace

int main(int argc, char** argv)
{
    auto fixtures = fixturesExpected(argc, argv);
    if (!fixtures)
    {
        std::fprintf(stderr, "%s\n", fixtures.error().c_str());
        return 1;
    }
    auto device = vultra::Device::create(true);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    const auto& programs = fixtures->programs;
    if (auto checked = run(**device, programs[0], programs[1], programs[2], programs[3], fixtures->reloadFile);
        !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
