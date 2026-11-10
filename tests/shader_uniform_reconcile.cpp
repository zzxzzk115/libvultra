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

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;
    const std::array entries {ShaderEntry {"main", VriShaderStage_Compute}};
    const std::array names {std::string_view("parameters")};

    struct Faults
    {
        Device&               device;
        VriCoreInterface      original;
        bool                  rejectMap      = false;
        bool                  rejectPipeline = false;
        inline static Faults* s_Active       = nullptr;

        explicit Faults(Device& device) :
            device(device),
            original(device.core)
        {
            s_Active                          = this;
            device.core.MapBuffer             = map;
            device.core.CreateComputePipeline = pipeline;
        }

        ~Faults()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static void* VRI_CALL map(VriBuffer* buffer, uint64_t offset, uint64_t size)
        {
            return s_Active->rejectMap ? nullptr : s_Active->original.MapBuffer(buffer, offset, size);
        }

        static VriResult VRI_CALL pipeline(VriDevice* device, const VriComputePipelineDesc* desc, VriPipeline** output)
        {
            const auto status = s_Active->original.CreateComputePipeline(device, desc, output);
            return status == VriResult_Success && s_Active->rejectPipeline ? VriResult_OutOfMemory : status;
        }
    };

    struct View
    {
        Device&        device;
        VriDescriptor* handle = nullptr;

        ~View()
        {
            if (handle)
            {
                device.core.DestroyDescriptor(handle);
            }
        }
    };

    struct Gpu
    {
        std::unique_ptr<ShaderBindings> bindings;
        std::unique_ptr<ShaderPipeline> pipeline;
    };

    std::expected<std::unique_ptr<Gpu>, std::string>
    prepare(Device& device, const ShaderProgram& program, std::unique_ptr<ShaderBindings> bindings)
    {
        auto candidate      = std::make_unique<Gpu>();
        candidate->bindings = std::move(bindings);
        auto pipeline       = ShaderPipeline::create(
            device,
            program,
            {entries.begin(), entries.end()},
            [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                const VriComputePipelineDesc desc {candidate->bindings->layout(),
                                                   shaders.front(),
                                                   device.pipelineCache};
                VriPipeline*                 value  = nullptr;
                const auto                   status = device.core.CreateComputePipeline(device.handle, &desc, &value);
                if (status != VriResult_Success || !value)
                {
                    if (value)
                    {
                        device.core.DestroyPipeline(value);
                    }
                    return std::unexpected("Create reconciled fixture pipeline failed");
                }
                return value;
            });
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        candidate->pipeline = std::move(*pipeline);
        return candidate;
    }

    Result run(Device& device, const std::array<ShaderProgram, 2>& programs)
    {
        const auto* oldLayout = programs[0].parameters.field("parameters");
        const auto* newLayout = programs[1].parameters.field("parameters");
        if (!oldLayout || !newLayout)
        {
            return std::unexpected("Missing reconciled uniform fixture layout");
        }
        const auto* oldElement = oldLayout->field("$element");
        const auto* newElement = newLayout->field("$element");
        const auto* oldItems   = oldElement ? oldElement->field("items") : nullptr;
        const auto* newItems   = newElement ? newElement->field("items") : nullptr;
        const auto* oldBasis = oldItems && oldItems->fields.size() == 1 ? oldItems->fields[0].field("basis") : nullptr;
        const auto* newBasis = newItems && newItems->fields.size() == 1 ? newItems->fields[0].field("basis") : nullptr;
        if (!oldItems || !newItems || !oldBasis || !newBasis || oldItems->count != 2 || newItems->count != 3 ||
            oldBasis->columnMajor || !newBasis->columnMajor)
        {
            return std::unexpected("Slang fixture did not change array count and matrix major order");
        }
        auto values = ShaderUniformBlock::create(*oldLayout);
        if (!values)
        {
            return std::unexpected(values.error());
        }
        const std::array multiplier {uint32_t(2)};
        const std::array bias {int32_t(-3)};
        const std::array enabled {true};
        const std::array gain {1.f};
        const std::array uv0 {4.f, 5.f};
        const std::array uv1 {7.f, 8.f};
        const std::array basis0 {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
        const std::array basis1 {7.f, 8.f, 9.f, 10.f, 11.f, 12.f};
        const std::array obsolete {uint32_t(13)};
        const std::array changed {uint32_t(17)};
        const std::array edits {
            ShaderUniformWrite {"multiplier", std::span(multiplier)},
            ShaderUniformWrite {"bias", std::span(bias)},
            ShaderUniformWrite {"enabled", std::span(enabled)},
            ShaderUniformWrite {"gain", std::span(gain)},
            ShaderUniformWrite {"items[0].uv", std::span(uv0), ShaderParameterKind::eVector, 1, 2},
            ShaderUniformWrite {"items[1].uv", std::span(uv1), ShaderParameterKind::eVector, 1, 2},
            ShaderUniformWrite {"items[0].basis", std::span(basis0), ShaderParameterKind::eMatrix, 2, 3},
            ShaderUniformWrite {"items[1].basis", std::span(basis1), ShaderParameterKind::eMatrix, 2, 3},
            ShaderUniformWrite {"obsolete", std::span(obsolete)},
            ShaderUniformWrite {"changed", std::span(changed)}};
        if (auto edited = values->setExpected(edits); !edited)
        {
            return std::unexpected(edited.error());
        }
        const std::vector<std::byte> original(values->data().begin(), values->data().end());
        auto                         output = Buffer::create(
            device,
            {16, 16, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
        if (!output)
        {
            return std::unexpected(output.error());
        }
        View                    view {device};
        const VriBufferViewDesc desc {(*output)->handle, VriDescriptorType_StorageBuffer, VriFormat_Unknown, 0, 16};
        if (device.core.CreateBufferView(device.handle, &desc, &view.handle) != VriResult_Success || !view.handle)
        {
            return std::unexpected("Create reconcile fixture output view failed");
        }
        const std::array resources {ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {view.handle}}};
        Faults           faults(device);
        auto             bindings = ShaderBindings::create(device, programs[0], entries, names);
        if (!bindings)
        {
            return std::unexpected(bindings.error());
        }
        std::string retained = "retained";
        if ((*bindings)->uniformBlockExpected(names[0]) ||
            (*bindings)->reconcileExpected(programs[1], entries, names, resources, retained) || retained != "retained")
        {
            return std::unexpected("Uninitialized bindings exposed accepted values or created a replacement");
        }
        {
            const std::array uploads {ShaderUniformUpload {names[0], *values}};
            if (auto uploaded = (*bindings)->updateExpected(resources, uploads); !uploaded)
            {
                return uploaded;
            }
        }
        auto current = prepare(device, programs[0], std::move(*bindings));
        if (!current)
        {
            return std::unexpected(current.error());
        }
        // Replacement must use the owner's accepted bytes after the caller releases its original block.
        values     = std::unexpected("Caller-side uniform block released");
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto execute = [&](std::array<uint32_t, 4> expected) -> Result
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            auto uniforms = (*current)->bindings->uniformBufferExpected(names[0]);
            if (!uniforms)
            {
                return std::unexpected(uniforms.error());
            }
            if (!(*uniforms)->transitionExpected(*commands,
                                                 {VriAccess_ConstantBufferRead, VriPipelineStage_ComputeShader}) ||
                !(*output)->transitionExpected(*commands,
                                               {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader}))
            {
                return std::unexpected("Transition reconcile fixture resources failed");
            }
            if (auto bound = (*current)->bindings->bindExpected(*commands, (*current)->pipeline->handle()); !bound)
            {
                return bound;
            }
            const VriDispatchDesc dispatch {1, 1, 1};
            device.core.CmdDispatch(*commands, &dispatch);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            auto bytes = readbackBufferExpected(device, **output);
            if (!bytes || bytes->bytes.size() != 16)
            {
                return std::unexpected("Read reconciled fixture output failed");
            }
            std::array<uint32_t, 4> actual;
            std::memcpy(actual.data(), bytes->bytes.data(), 16);
            if (actual != expected)
            {
                return std::unexpected("Reconciled GPU values differ: " + std::to_string(actual[0]) + "," +
                                       std::to_string(actual[1]) + "," + std::to_string(actual[2]) + "," +
                                       std::to_string(actual[3]));
            }
            return {};
        };
        if (auto checked = execute({17, 14, 12, 30}); !checked)
        {
            return checked;
        }
        auto accepted = (*current)->bindings->uniformBlockExpected(names[0]);
        if (!accepted || accepted->revision() != 0 || !std::ranges::equal(accepted->data(), original) ||
            (*current)->bindings->uniformBlockExpected("result") ||
            (*current)->bindings->validateLayoutExpected(programs[1], entries))
        {
            return std::unexpected("Accepted uniform snapshot or changed-layout isolation failed");
        }
        const std::array different {uint32_t(99)};
        const std::array snapshotEdit {ShaderUniformWrite {"multiplier", std::span(different)}};
        if (!accepted->setExpected(snapshotEdit))
        {
            return std::unexpected("Edit independent uniform snapshot failed");
        }
        auto invalidResources    = resources;
        invalidResources[0].type = VriDescriptorType_Texture;
        if ((*current)->bindings->reconcileExpected(programs[1], entries, names, invalidResources, retained) ||
            retained != "retained")
        {
            return std::unexpected("Invalid replacement resources published diagnostics or bindings");
        }
        const auto replacement = [&](std::string& diagnostics) -> std::expected<std::unique_ptr<Gpu>, std::string>
        {
            std::string messages;
            auto prepared = (*current)->bindings->reconcileExpected(programs[1], entries, names, resources, messages);
            if (!prepared)
            {
                return std::unexpected(prepared.error());
            }
            auto gpu = prepare(device, programs[1], std::move(*prepared));
            if (!gpu)
            {
                return std::unexpected(gpu.error());
            }
            diagnostics += messages;
            return gpu;
        };
        std::string messages;
        for (unsigned test = 0; test < 2; ++test)
        {
            const auto oldPipeline     = (*current)->pipeline->handle();
            const auto oldLayoutHandle = (*current)->bindings->layout();
            faults.rejectMap           = test == 0;
            faults.rejectPipeline      = test == 1;
            auto rejected              = replacement(messages);
            faults.rejectMap           = false;
            faults.rejectPipeline      = false;
            if (rejected || (*current)->pipeline->handle() != oldPipeline ||
                (*current)->bindings->layout() != oldLayoutHandle || !messages.empty())
            {
                return std::unexpected("Failed layout candidate replaced live uniform/pipeline state");
            }
            if (auto checked = execute({17, 14, 12, 30}); !checked)
            {
                return checked;
            }
        }
        auto candidate = replacement(messages);
        if (!candidate)
        {
            return std::unexpected(candidate.error());
        }
        if (messages.find("Reset incompatible uniform field: changed") == std::string::npos ||
            messages.find("Removed uniform field: obsolete") == std::string::npos ||
            messages.find("Initialized new uniform field: fresh") == std::string::npos)
        {
            return std::unexpected("Shared binding replacement omitted layout migration diagnostics");
        }
        current->swap(*candidate);
        candidate->reset();
        if (auto checked = execute({17, 14, 12, 0}); !checked)
        {
            return checked;
        }
        auto candidateValues = (*current)->bindings->uniformBlockExpected(names[0]);
        if (!candidateValues)
        {
            return std::unexpected(candidateValues.error());
        }
        const std::array defaultChanged {2.f};
        const std::array fresh {uint32_t(9)};
        const std::array uv2 {2.f, 3.f};
        const std::array basis2 {13.f, 14.f, 15.f, 16.f, 17.f, 18.f};
        const std::array defaults {
            ShaderUniformWrite {"changed", std::span(defaultChanged)},
            ShaderUniformWrite {"fresh", std::span(fresh)},
            ShaderUniformWrite {"items[2].uv", std::span(uv2), ShaderParameterKind::eVector, 1, 2},
            ShaderUniformWrite {"items[2].basis", std::span(basis2), ShaderParameterKind::eMatrix, 2, 3}};
        if (auto set = candidateValues->setExpected(defaults); !set)
        {
            return std::unexpected(set.error());
        }
        const std::array uploads {ShaderUniformUpload {names[0], *candidateValues}};
        if (auto uploaded = (*current)->bindings->updateExpected(resources, uploads); !uploaded)
        {
            return uploaded;
        }
        if (auto checked = execute({17, 14, 12, 31}); !checked)
        {
            return checked;
        }
        std::puts("UNIFORM_RECONCILE_GPU exact=17,14,12,30/17,14,12,0/17,14,12,31 "
                  "matrix_major=1 array_grow=1 type_reset=1 candidates_rejected=2 old_state=retained "
                  "shared_candidates=1 accepted_snapshot=1 caller_released=1");
        return {};
    }

    std::expected<std::array<ShaderProgram, 2>, std::string> programsExpected(int argc, char** argv)
    {
        std::array<ShaderProgram, 2> programs;
        if (argc == 3)
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
                auto program    = (*compiler)->compileExpected("tests/shaders/shader_uniform_reconcile.slang", options);
                if (!program)
                {
                    return std::unexpected(program.error());
                }
                programs[i] = std::move(*program);
            }
            return programs;
        }
#endif
        return std::unexpected("Usage: test-shader-uniform-reconcile old.vshaderc new.vshaderc");
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
    auto device = vultra::Device::create(true);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto checked = run(**device, *programs); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
