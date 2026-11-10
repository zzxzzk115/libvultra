#include <vultra/drivers/rhi/buffer_readback.hpp>
#include <vultra/drivers/rhi/shader_bindings.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/drivers/rhi/shader_uniform_block.hpp>

#ifdef VULTRA_WITH_SHADER_COMPILER
#include <vultra/drivers/rhi/shader_compiler.hpp>
#endif

#include <array>
#include <cstdio>
#include <cstring>
#include <set>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;
    const std::array entries {ShaderEntry {"main", VriShaderStage_Compute}};
    const std::array names {std::string_view("stage.parameters"), std::string_view("adjustments")};

    struct Faults
    {
        Device&                  device;
        VriCoreInterface         original;
        std::set<VriBuffer*>     buffers;
        std::set<VriDescriptor*> views;
        std::set<VriBuffer*>     mapped;
        unsigned                 rejectMap   = 0;
        unsigned                 rejectView  = 0;
        unsigned                 mapCalls    = 0;
        unsigned                 viewCalls   = 0;
        unsigned                 allocations = 0;
        unsigned                 updates     = 0;
        inline static Faults*    s_Active    = nullptr;

        explicit Faults(Device& device) :
            device(device),
            original(device.core)
        {
            s_Active                           = this;
            device.core.CreateBuffer           = createBuffer;
            device.core.DestroyBuffer          = destroyBuffer;
            device.core.CreateBufferView       = createView;
            device.core.DestroyDescriptor      = destroyView;
            device.core.MapBuffer              = map;
            device.core.UnmapBuffer            = unmap;
            device.core.UpdateDescriptorRanges = update;
        }

        ~Faults()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static VriResult VRI_CALL createBuffer(VriDevice* device, const VriBufferDesc* desc, VriBuffer** output)
        {
            auto& self = *s_Active;
            ++self.allocations;
            const auto status = self.original.CreateBuffer(device, desc, output);
            if (*output)
            {
                self.buffers.insert(*output);
            }
            return status;
        }

        static void VRI_CALL destroyBuffer(VriBuffer* buffer)
        {
            s_Active->buffers.erase(buffer);
            s_Active->original.DestroyBuffer(buffer);
        }

        static VriResult VRI_CALL createView(VriDevice* device, const VriBufferViewDesc* desc, VriDescriptor** output)
        {
            auto& self = *s_Active;
            ++self.viewCalls;
            ++self.allocations;
            const auto status = self.original.CreateBufferView(device, desc, output);
            if (*output)
            {
                self.views.insert(*output);
            }
            return status == VriResult_Success && self.rejectView == self.viewCalls ? VriResult_OutOfMemory : status;
        }

        static void VRI_CALL destroyView(VriDescriptor* view)
        {
            s_Active->views.erase(view);
            s_Active->original.DestroyDescriptor(view);
        }

        static void* VRI_CALL map(VriBuffer* buffer, uint64_t offset, uint64_t size)
        {
            auto& self = *s_Active;
            if (++self.mapCalls == self.rejectMap)
            {
                return nullptr;
            }
            auto* data = self.original.MapBuffer(buffer, offset, size);
            if (data)
            {
                self.mapped.insert(buffer);
            }
            return data;
        }

        static void VRI_CALL unmap(VriBuffer* buffer)
        {
            s_Active->mapped.erase(buffer);
            s_Active->original.UnmapBuffer(buffer);
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
        std::vector<VriDescriptor*> values;

        ~Views()
        {
            for (auto* value : values)
            {
                device.core.DestroyDescriptor(value);
            }
        }
    };

    Result run(Device& device, const ShaderProgram& program)
    {
        const auto* stage        = program.parameters.field("stage");
        const auto* firstLayout  = stage ? stage->field("parameters") : nullptr;
        const auto* secondLayout = program.parameters.field("adjustments");
        if (!firstLayout || !secondLayout || secondLayout->kind != ShaderParameterKind::eArray ||
            secondLayout->count != 1 || secondLayout->fields.size() != 1)
        {
            return std::unexpected("Missing uniform-storage fixture blocks");
        }
        auto first  = ShaderUniformBlock::create(*firstLayout);
        auto second = ShaderUniformBlock::create(secondLayout->fields.front());
        if (!first || !second)
        {
            return std::unexpected("Create typed uniform-storage fixture blocks failed");
        }
        const std::array multiplier {uint32_t(2)};
        const std::array bias {int32_t(-3)};
        const std::array enabled {true};
        const std::array gain {1.f};
        const std::array offset {uint32_t(5)};
        const std::array edits {ShaderUniformWrite {"multiplier", std::span(multiplier)},
                                ShaderUniformWrite {"bias", std::span(bias)},
                                ShaderUniformWrite {"enabled", std::span(enabled)},
                                ShaderUniformWrite {"gain", std::span(gain)}};
        const std::array adjustment {ShaderUniformWrite {"offset", std::span(offset)}};
        if (!first->setExpected(edits) || !second->setExpected(adjustment))
        {
            return std::unexpected("Pack typed uniform-storage fixture failed");
        }
        std::array<std::unique_ptr<Buffer>, 3> buffers;
        const std::array                       descriptors {
            VriBufferDesc {16, 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload},
            VriBufferDesc {16, 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload},
            VriBufferDesc {4, 4, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device}};
        Views views {device, {}};
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            auto buffer = Buffer::create(device, descriptors[i]);
            if (!buffer)
            {
                return std::unexpected(buffer.error());
            }
            buffers[i]                   = std::move(*buffer);
            const auto              type = i < 2 ? VriDescriptorType_ConstantBuffer : VriDescriptorType_StorageBuffer;
            const VriBufferViewDesc desc {buffers[i]->handle, type, VriFormat_Unknown, 0, descriptors[i].size};
            VriDescriptor*          view   = nullptr;
            const auto              status = device.core.CreateBufferView(device.handle, &desc, &view);
            if (view)
            {
                views.values.push_back(view);
            }
            if (status != VriResult_Success || !view)
            {
                return std::unexpected("Create borrowed uniform-storage fixture view failed");
            }
            if (i < 2)
            {
                const std::array<uint32_t, 4> data {i == 0 ? 7u : 29u, 0, 0, 0};
                auto*                         destination = device.core.MapBuffer(buffers[i]->handle, 0, sizeof(data));
                if (!destination)
                {
                    return std::unexpected("Map borrowed frame uniform fixture failed");
                }
                std::memcpy(destination, data.data(), sizeof(data));
                device.core.UnmapBuffer(buffers[i]->handle);
            }
        }
        Faults faults(device);
        for (unsigned rejected : {1u, 2u})
        {
            faults.viewCalls  = 0;
            faults.rejectView = rejected;
            auto candidate    = ShaderBindings::create(device, program, entries, names);
            faults.rejectView = 0;
            if (candidate || !faults.buffers.empty() || !faults.views.empty())
            {
                return std::unexpected("Partial owned-uniform view creation leaked buffers/views");
            }
        }
        const std::array invalidNames {std::string_view("unknown"), std::string_view("result")};
        for (const auto name : invalidNames)
        {
            const std::array selected {name};
            if (ShaderBindings::create(device, program, entries, selected))
            {
                return std::unexpected("Unknown or non-uniform ownership selection was accepted");
            }
        }
        const std::array duplicates {names[0], names[0]};
        if (ShaderBindings::create(device, program, entries, duplicates))
        {
            return std::unexpected("Duplicate uniform ownership was accepted");
        }
        auto owner = ShaderBindings::create(device, program, entries, names);
        if (!owner)
        {
            return std::unexpected(owner.error());
        }
        auto firstBuffer  = (*owner)->uniformBufferExpected(names[0]);
        auto secondBuffer = (*owner)->uniformBufferExpected(names[1]);
        if (!firstBuffer || !secondBuffer || *firstBuffer == *secondBuffer ||
            (*owner)->uniformBufferExpected("frame") || (*owner)->uniformBufferExpected("unknown"))
        {
            return std::unexpected("Owned uniform buffer lookup lost ownership or accepted borrowed/unknown storage");
        }
        const std::array ownedBuffers {*firstBuffer, *secondBuffer};
        const auto       makePipeline = [&](ShaderBindings& selected)
        {
            return ShaderPipeline::create(
                device,
                program,
                {entries.begin(), entries.end()},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    const VriComputePipelineDesc desc {selected.layout(), shaders.front(), device.pipelineCache};
                    VriPipeline*                 value = nullptr;
                    const auto status = device.core.CreateComputePipeline(device.handle, &desc, &value);
                    if (status != VriResult_Success || !value)
                    {
                        if (value)
                        {
                            device.core.DestroyPipeline(value);
                        }
                        return std::unexpected("Create owned-uniform fixture pipeline failed");
                    }
                    return value;
                });
        };
        auto pipeline = makePipeline(**owner);
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        std::array resources {ShaderResourceViews {"frame", VriDescriptorType_ConstantBuffer, {views.values[0]}},
                              ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {views.values[2]}}};
        const auto uploads = [&]()
        {
            return std::array {ShaderUniformUpload {names[0], *first}, ShaderUniformUpload {names[1], *second}};
        };
        if ((*owner)->updateExpected(resources))
        {
            return std::unexpected("Uninitialized owned uniform bytes were accepted");
        }
        if (auto updated = (*owner)->updateExpected(resources, uploads()); !updated)
        {
            return updated;
        }
        bool       ownsFrame = false;
        const auto execute = [&](uint32_t expected) -> Result
        {
            auto command = (*frame)->beginExpected();
            if (!command)
            {
                return std::unexpected(command.error());
            }
            for (size_t i = 0; i < buffers.size(); ++i)
            {
                const VriAccessStage state {i < 2 ? VriAccess_ConstantBufferRead : VriAccess_ShaderResourceStorageWrite,
                                            VriPipelineStage_ComputeShader};
                if (auto changed = buffers[i]->transitionExpected(*command, state); !changed)
                {
                    return changed;
                }
            }
            const std::array selected {names[0], names[1], std::string_view("frame")};
            for (const auto name : std::span(selected).first(ownsFrame ? 3 : 2))
            {
                auto buffer = (*owner)->uniformBufferExpected(name);
                if (!buffer)
                {
                    return std::unexpected(buffer.error());
                }
                if (auto changed =
                        (*buffer)->transitionExpected(*command,
                                                      {VriAccess_ConstantBufferRead, VriPipelineStage_ComputeShader});
                    !changed)
                {
                    return changed;
                }
            }
            if (auto bound = (*owner)->bindExpected(*command, (*pipeline)->handle()); !bound)
            {
                return bound;
            }
            const VriDispatchDesc dispatch {1, 1, 1};
            device.core.CmdDispatch(*command, &dispatch);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            auto data = readbackBufferExpected(device, *buffers[2]);
            if (!data || data->bytes.size() != 4)
            {
                return std::unexpected("Read owned-uniform fixture output failed");
            }
            uint32_t value;
            std::memcpy(&value, data->bytes.data(), 4);
            if (value != expected)
            {
                return std::unexpected("Owned uniform GPU value differs: " + std::to_string(value));
            }
            return {};
        };
        if (auto checked = execute(16); !checked)
        {
            return checked;
        }
        const auto retainedBuffers = faults.buffers;
        const auto retainedViews   = faults.views;
        for (unsigned failure : {1u, 2u})
        {
            faults.viewCalls  = 0;
            faults.rejectView = failure;
            auto rejected     = (*owner)->createInstanceExpected();
            faults.rejectView = 0;
            if (rejected || faults.buffers != retainedBuffers || faults.views != retainedViews)
            {
                return std::unexpected("Binding instance uniform failure changed or leaked the layout owner");
            }
        }
        {
            auto instance = (*owner)->createInstanceExpected();
            if (!instance)
            {
                return std::unexpected(instance.error());
            }
            auto buffer = (*instance)->uniformBufferExpected(names[0]);
            if (!buffer || *buffer == *firstBuffer || (*instance)->layout() != (*owner)->layout() ||
                (*instance)->uniformBlockExpected(names[0]) || (*instance)->updateExpected(resources))
            {
                return std::unexpected(
                    "Binding instance did not borrow the layout with independent unprepared storage");
            }
            auto             firstCopy  = *first;
            auto             secondCopy = *second;
            const std::array instanceMultiplier {4u};
            const std::array instanceOffset {11u};
            const std::array instanceEdit {ShaderUniformWrite {"multiplier", std::span(instanceMultiplier)}};
            const std::array offsetEdit {ShaderUniformWrite {"offset", std::span(instanceOffset)}};
            if (!firstCopy.setExpected(instanceEdit) || !secondCopy.setExpected(offsetEdit))
            {
                return std::unexpected("Pack independent binding instance values failed");
            }
            auto instanceResources        = resources;
            instanceResources[0].views[0] = views.values[1];
            const std::array instanceUploads {ShaderUniformUpload {names[0], firstCopy},
                                              ShaderUniformUpload {names[1], secondCopy}};
            if (auto updated = (*instance)->updateExpected(instanceResources, instanceUploads); !updated)
            {
                return updated;
            }
            if (auto checked = execute(16); !checked)
            {
                return checked;
            }
            // The existing pipeline still uses the parent's exact native layout while descriptors/values differ.
            auto parent  = std::move(*owner);
            *owner       = std::move(*instance);
            auto checked = execute(124);
            *instance    = std::move(*owner);
            *owner       = std::move(parent);
            if (!checked)
            {
                return checked;
            }
            if (auto restored = execute(16); !restored)
            {
                return restored;
            }
        }
        if (faults.buffers != retainedBuffers || faults.views != retainedViews)
        {
            return std::unexpected("Binding instance destruction changed the parent's uniform storage");
        }
        std::puts("Shared-layout binding instances: independent GPU values/resources, partial-allocation cleanup and "
                  "parent retention passed");
        const std::array nextMultiplier {uint32_t(3)};
        const std::array nextOffset {uint32_t(9)};
        const std::array nextFirst {ShaderUniformWrite {"multiplier", std::span(nextMultiplier)}};
        const std::array nextSecond {ShaderUniformWrite {"offset", std::span(nextOffset)}};
        if (!first->setExpected(nextFirst) || !second->setExpected(nextSecond))
        {
            return std::unexpected("Stage owned-uniform edits failed");
        }
        resources[0].views[0] = views.values[1];
        for (unsigned test = 0; test < 3; ++test)
        {
            auto layout = first->layout();
            if (test == 0)
            {
                layout.fields[0].scalar   = ShaderScalarType::eFloat;
                layout.fields[0].typeName = "float";
            }
            else if (test == 1)
            {
                layout.fields[0].kind = ShaderParameterKind::eVector;
            }
            else
            {
                std::swap(layout.fields[0].offsets, layout.fields[1].offsets);
            }
            auto incompatible = ShaderUniformBlock::create(layout);
            if (!incompatible || incompatible->data().size() != first->data().size())
            {
                return std::unexpected("Typed-layout rejection fixture changed its payload size");
            }
            auto invalid       = uploads();
            invalid[0]         = ShaderUniformUpload {names[0], *incompatible};
            const auto maps    = faults.mapCalls;
            const auto updates = faults.updates;
            if ((*owner)->validateExpected(resources, invalid) || (*owner)->updateExpected(resources, invalid) ||
                faults.mapCalls != maps || faults.updates != updates)
            {
                return std::unexpected("Same-sized typed layout mismatch mutated the uniform/resource batch");
            }
            if (auto checked = execute(16); !checked)
            {
                return checked;
            }
        }
        for (unsigned test = 0; test < 3; ++test)
        {
            auto invalid = uploads();
            if (test == 0)
            {
                invalid[1].bytes = invalid[1].bytes.first(1);
            }
            else
            {
                invalid[1].name = test == 1 ? names[0] : "frame";
            }
            const auto maps    = faults.mapCalls;
            const auto updates = faults.updates;
            if ((*owner)->updateExpected(resources, invalid) || faults.mapCalls != maps || faults.updates != updates)
            {
                return std::unexpected("Invalid uniform batch changed GPU bindings/data");
            }
            if (auto checked = execute(16); !checked)
            {
                return checked;
            }
        }
        faults.mapCalls     = 0;
        faults.rejectMap    = 2;
        const auto updates  = faults.updates;
        auto       rejected = (*owner)->updateExpected(resources, uploads());
        faults.rejectMap    = 0;
        if (rejected || faults.mapCalls != 2 || !faults.mapped.empty() || faults.updates != updates)
        {
            return std::unexpected("Second owned-buffer map failure did not roll back the complete batch");
        }
        if (auto checked = execute(16); !checked)
        {
            return checked;
        }
        const auto allocations = faults.allocations;
        if (auto updated = (*owner)->updateExpected(resources, uploads()); !updated)
        {
            return updated;
        }
        if (faults.allocations != allocations)
        {
            return std::unexpected("Owned uniform update recreated buffers or views");
        }
        if (auto checked = execute(93); !checked)
        {
            return checked;
        }
        const auto maps = faults.mapCalls;
        if (!(*owner)->updateExpected(resources, uploads()) || faults.mapCalls != maps)
        {
            return std::unexpected("Identical uniform bytes were mapped again");
        }
        resources[0].views[0] = views.values[0];
        if (!(*owner)->updateExpected(resources) || faults.mapCalls != maps)
        {
            return std::unexpected("Accepted uniform bytes were not reusable without uploads");
        }
        if (auto checked = execute(27); !checked)
        {
            return checked;
        }
        const std::array partialOffset {uint32_t(15)};
        const std::array partialEdit {ShaderUniformWrite {"offset", std::span(partialOffset)}};
        if (auto staged = second->setExpected(partialEdit); !staged)
        {
            return std::unexpected(staged.error());
        }
        const std::array partialUpload {ShaderUniformUpload {names[1], second->data()}};
        const auto       beforePartial = faults.mapCalls;
        if (!(*owner)->updateExpected(resources, partialUpload) || faults.mapCalls != beforePartial + 1)
        {
            return std::unexpected("Partial uniform update did not reuse the other accepted block");
        }
        if (auto checked = execute(33); !checked)
        {
            return checked;
        }
        if ((*owner)->uniformBufferExpected(names[0]).value_or(nullptr) != ownedBuffers[0] ||
            (*owner)->uniformBufferExpected(names[1]).value_or(nullptr) != ownedBuffers[1])
        {
            return std::unexpected("Accepted uniform updates replaced graph-visible buffer wrappers");
        }
        const std::array expanded {names[0], names[1], std::string_view("frame")};
        const std::array reducedResources {resources[1]};
        std::string      diagnostics = "retained";
        faults.mapCalls              = 0;
        faults.rejectMap             = 2;
        auto rejectedCandidate = (*owner)->reconcileExpected(program, entries, expanded, reducedResources, diagnostics);
        faults.rejectMap       = 0;
        if (rejectedCandidate || diagnostics != "retained" || faults.mapCalls != 2 || !faults.mapped.empty() ||
            faults.buffers.size() != 2 || faults.views.size() != 2)
        {
            return std::unexpected("Failed multi-uniform candidate changed old state or leaked candidate storage");
        }
        if (auto checked = execute(33); !checked)
        {
            return checked;
        }
        const auto publish = [&](std::unique_ptr<ShaderBindings> replacement) -> Result
        {
            auto candidatePipeline = makePipeline(*replacement);
            if (!candidatePipeline)
            {
                return std::unexpected(candidatePipeline.error());
            }
            pipeline->swap(*candidatePipeline);
            owner->swap(replacement);
            candidatePipeline->reset();
            replacement.reset();
            return {};
        };
        auto candidate = (*owner)->reconcileExpected(program, entries, expanded, reducedResources, diagnostics);
        if (!candidate || diagnostics.find("Initialized owned uniform: frame") == std::string::npos)
        {
            return std::unexpected(candidate ? "New owned uniform initialization was not reported" : candidate.error());
        }
        if (auto published = publish(std::move(*candidate)); !published)
        {
            return published;
        }
        ownsFrame = true;
        if (auto checked = execute(15); !checked)
        {
            return checked;
        }
        auto             frameValues = (*owner)->uniformBlockExpected("frame");
        const std::array base {uint32_t(7)};
        const std::array frameEdit {ShaderUniformWrite {"base", std::span(base)}};
        if (!frameValues || !frameValues->setExpected(frameEdit))
        {
            return std::unexpected("Edit newly owned frame uniform failed");
        }
        const std::array frameUpload {ShaderUniformUpload {"frame", *frameValues}};
        if (auto uploaded = (*owner)->updateExpected(reducedResources, frameUpload); !uploaded)
        {
            return uploaded;
        }
        if (auto checked = execute(33); !checked)
        {
            return checked;
        }
        diagnostics.clear();
        candidate = (*owner)->reconcileExpected(program, entries, names, resources, diagnostics);
        if (!candidate || diagnostics.find("Released owned uniform: frame") == std::string::npos)
        {
            return std::unexpected(candidate ? "Released uniform ownership was not reported" : candidate.error());
        }
        if (auto published = publish(std::move(*candidate)); !published)
        {
            return published;
        }
        ownsFrame = false;
        if ((*owner)->uniformBlockExpected("frame"))
        {
            return std::unexpected("Borrowed constant buffer exposed an owned uniform snapshot");
        }
        if (auto checked = execute(33); !checked)
        {
            return checked;
        }
        frame->reset();
        pipeline->reset();
        owner->reset();
        if (!faults.buffers.empty() || !faults.views.empty() || !faults.mapped.empty())
        {
            return std::unexpected("Owned uniform storage leaked native objects or mappings");
        }
        std::puts("SHADER_UNIFORM_STORAGE exact=16,93,27,33 owned_blocks=2 borrowed_cb=1 atomic_map=1 "
                  "atomic_validation=3 typed_layout_rejected=3 nested_binding=1 cb_array_one=1 "
                  "native_partial_views=2 reuse=1 identity_skip=1 "
                  "partial=1 explicit_buffers=1 candidate_map_atomic=1 owned_transition=2 candidate_exact=15,33,33");
        return {};
    }

    std::expected<ShaderProgram, std::string> programExpected(int argc, char** argv)
    {
        if (argc == 2)
        {
            return ShaderProgram::loadExpected(argv[1]);
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
            return (*compiler)->compileExpected("tests/shaders/shader_uniform_storage.slang", options);
        }
#endif
        return std::unexpected("Usage: test-shader-uniform-storage probe.vshaderc");
    }
} // namespace

int main(int argc, char** argv)
{
    auto program = programExpected(argc, argv);
    if (!program)
    {
        std::fprintf(stderr, "%s\n", program.error().c_str());
        return 1;
    }
    auto device = vultra::Device::create(true);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto checked = run(**device, *program); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
