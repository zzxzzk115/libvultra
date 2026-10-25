#include <vultra/drivers/compute/compute_context.hpp>
#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/drivers/rhi/shader_program.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <set>

namespace vultra
{
    namespace
    {
        // Identity allocation only. All resources and device state belong to their context.
        std::atomic<uint64_t> nextIdentity {1};
        constexpr uint64_t    kIdentityLimit = uint64_t(1) << 56;

        uint64_t identity(bool kernel)
        {
            auto value = nextIdentity.load(std::memory_order_relaxed);
            do
            {
                if (value >= kIdentityLimit)
                {
                    throw std::overflow_error("Compute resource identity exhausted");
                }
            } while (!nextIdentity.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));
            return value | (uint64_t(kernel ? 4 : 3) << 56);
        }

        std::string scalarName(const ShaderParameter& parameter)
        {
            if (parameter.kind == ShaderParameterKind::eScalar && parameter.size == 4)
            {
                switch (parameter.scalar)
                {
                    case ShaderScalarType::eFloat:
                        return "float";
                    case ShaderScalarType::eInteger:
                        return "int";
                    case ShaderScalarType::eUnsigned:
                        return "uint";
                    default:
                        break;
                }
            }
            throw std::invalid_argument("Compute currently requires float/int/uint scalar elements: " +
                                        parameter.typeName);
        }

        const ShaderParameter& parameterAt(const ShaderParameter& root, std::string_view name)
        {
            const auto  dot  = name.find('.');
            const auto* node = root.field(name.substr(0, dot));
            if (!node)
            {
                throw std::invalid_argument("Missing compute parameter layout: " + std::string(name));
            }
            return dot == std::string_view::npos ? *node : parameterAt(*node, name.substr(dot + 1));
        }

        struct ComputeBuffer
        {
            Device&                       device;
            Buffer                        storage;
            std::string                   scalar;
            bool                          initialized = false;
            std::array<VriDescriptor*, 2> views {};

            ComputeBuffer(Device& owner, uint64_t count, std::string_view type) :
                device(owner),
                storage(owner,
                        {count * 4,
                         4,
                         VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst,
                         VriMemoryLocation_Device}),
                scalar(type)
            {
                try
                {
                    for (uint32_t index = 0; index < views.size(); ++index)
                    {
                        const VriBufferViewDesc desc {storage.handle,
                                                      index == 0 ? VriDescriptorType_StructuredBuffer :
                                                                   VriDescriptorType_StorageBuffer,
                                                      VriFormat_Unknown,
                                                      0,
                                                      storage.desc.size};
                        check(device.core.CreateBufferView(device.handle, &desc, &views[index]),
                              "Create compute buffer view");
                    }
                }
                catch (...)
                {
                    release();
                    throw;
                }
            }

            ~ComputeBuffer()
            {
                release();
            }

            ComputeBuffer(const ComputeBuffer&)            = delete;
            ComputeBuffer& operator=(const ComputeBuffer&) = delete;

            void release()
            {
                for (auto* view : views)
                {
                    if (view)
                    {
                        device.core.DestroyDescriptor(view);
                    }
                }
            }
        };

        struct ComputeKernel
        {
            Device&                            device;
            ShaderProgram                      program;
            std::vector<ShaderResourceBinding> buffers;
            std::vector<std::string>           scalars;
            uint32_t                           constantsSize = 0;
            VriPipelineLayout*                 layout        = nullptr;
            VriDescriptorPool*                 pool          = nullptr;
            VriDescriptorSet*                  set           = nullptr;
            VriPipeline*                       pipeline      = nullptr;
            nlohmann::json                     metadata;

            ComputeKernel(Device& owner, ShaderProgram compiled) :
                device(owner),
                program(std::move(compiled))
            {
                try
                {
                    metadata = {{"version", 1},
                                {"thread_group", program.shaders.front().threadGroup},
                                {"buffers", nlohmann::json::array()},
                                {"constants", nullptr}};
                    for (const auto& binding : program.resourceBindings())
                    {
                        const auto& node    = parameterAt(program.parameters, binding.name);
                        const auto* element = node.field("$element");
                        if (binding.pushConstant && element && !constantsSize)
                        {
                            if (element->kind != ShaderParameterKind::eStruct || element->fields.empty() ||
                                !element->size || element->size > 128 || element->size % 4 != 0)
                            {
                                throw std::invalid_argument(
                                    "Compute push constants must occupy 4 to 128 aligned bytes");
                            }
                            constantsSize = uint32_t(element->size);
                            auto fields   = nlohmann::json::array();
                            for (const auto& field : element->fields)
                            {
                                fields.push_back({{"name", field.name},
                                                  {"type", scalarName(field)},
                                                  {"offset", field.offset(ShaderOffsetKind::eUniform)}});
                            }
                            metadata["constants"] = {{"name", binding.name},
                                                     {"size", constantsSize},
                                                     {"fields", fields}};
                        }
                        else if (binding.set == 0 && binding.count == 1 && element &&
                                 (binding.type == VriDescriptorType_StructuredBuffer ||
                                  binding.type == VriDescriptorType_StorageBuffer))
                        {
                            buffers.push_back(binding);
                            scalars.push_back(scalarName(*element));
                            metadata["buffers"].push_back(
                                {{"name", binding.name},
                                 {"type", scalars.back()},
                                 {"writable", binding.type == VriDescriptorType_StorageBuffer}});
                        }
                        else
                        {
                            throw std::invalid_argument(
                                "Unsupported compute binding: " + binding.name +
                                "; use scalar structured buffers in set 0 and one push-constant block");
                        }
                    }
                    std::vector<VriDescriptorRangeDesc> ranges;
                    VriDescriptorPoolDesc               poolDesc {};
                    poolDesc.descriptorSetMaxNum = 1;
                    for (const auto& binding : buffers)
                    {
                        ranges.push_back({binding.binding, 1, binding.type, VriShaderStage_Compute});
                        if (binding.type == VriDescriptorType_StorageBuffer)
                        {
                            ++poolDesc.storageBufferMaxNum;
                        }
                        else
                        {
                            ++poolDesc.structuredBufferMaxNum;
                        }
                    }
                    const VriDescriptorSetDesc setDesc {0, ranges.data(), uint32_t(ranges.size())};
                    const VriPushConstantDesc  push {0, constantsSize, VriShaderStage_Compute};
                    VriPipelineLayoutDesc      desc {};
                    desc.descriptorSets   = &setDesc;
                    desc.descriptorSetNum = 1;
                    desc.pushConstants    = &push;
                    desc.pushConstantNum  = constantsSize ? 1 : 0;
                    desc.shaderStages     = VriShaderStage_Compute;
                    check(device.core.CreatePipelineLayout(device.handle, &desc, &layout), "Create compute layout");
                    check(device.core.CreateDescriptorPool(device.handle, &poolDesc, &pool), "Create compute pool");
                    check(device.core.AllocateDescriptorSets(pool, layout, 0, &set, 1), "Allocate compute descriptors");
                    const std::array             entries {program.shaders.front().entry};
                    const auto                   shaders = program.descriptors(entries);
                    const VriComputePipelineDesc pipelineDesc {layout, shaders.front(), device.pipelineCache};
                    check(device.core.CreateComputePipeline(device.handle, &pipelineDesc, &pipeline),
                          "Create compute pipeline");
                }
                catch (...)
                {
                    release();
                    throw;
                }
            }

            ~ComputeKernel()
            {
                release();
            }

            ComputeKernel(const ComputeKernel&)            = delete;
            ComputeKernel& operator=(const ComputeKernel&) = delete;

            void release()
            {
                if (pipeline)
                {
                    device.core.DestroyPipeline(pipeline);
                }
                if (pool)
                {
                    device.core.DestroyDescriptorPool(pool);
                }
                if (layout)
                {
                    device.core.DestroyPipelineLayout(layout);
                }
            }
        };

        template<class T>
        auto& resource(T& resources, uint64_t id)
        {
            const auto found = resources.find(id);
            if (found == resources.end())
            {
                throw std::invalid_argument("Compute resource is foreign, released or has the wrong type");
            }
            return *found->second;
        }
    } // namespace

    struct ComputeContext::Impl
    {
        Device                                             device;
        Frame                                              frame;
        std::map<uint64_t, std::unique_ptr<ComputeBuffer>> buffers;
        std::map<uint64_t, std::unique_ptr<ComputeKernel>> kernels;
        bool                                               failed = false;

        explicit Impl(bool validation) :
            device(validation),
            frame(device)
        {
            if (!device.core.GetDeviceDesc(device.handle)->hasComputeShader)
            {
                throw std::runtime_error("Device does not support compute shaders");
            }
        }

        ~Impl()
        {
            // A failed completion may leave work outstanding. Finish before destroying descriptors/resources.
            device.waitIdleForDestruction();
            kernels.clear();
            buffers.clear();
        }

        void ready() const
        {
            if (failed)
            {
                throw std::runtime_error("Compute context stopped after GPU completion failure");
            }
        }

        void complete()
        {
            try
            {
                frame.submitAndWait();
            }
            catch (...)
            {
                failed = true;
                device.waitIdleForDestruction();
                throw;
            }
        }
    };

    ComputeContext::ComputeContext(bool validation) :
        m_Impl(std::make_unique<Impl>(validation))
    {
    }

    ComputeContext::~ComputeContext() = default;

    uint64_t ComputeContext::createBuffer(uint64_t count, std::string_view scalarType)
    {
        auto& impl = *m_Impl;
        impl.ready();
        if (!count || count > impl.device.core.GetDeviceDesc(impl.device.handle)->bufferMaxSize / 4 ||
            (scalarType != "float" && scalarType != "int" && scalarType != "uint"))
        {
            throw std::invalid_argument(
                "Compute buffer needs a supported scalar type and a positive count within device limits");
        }
        const auto id = identity(false);
        impl.buffers.emplace(id, std::make_unique<ComputeBuffer>(impl.device, count, scalarType));
        return id;
    }

    void ComputeContext::writeBuffer(uint64_t id, std::span<const std::byte> data)
    {
        auto& impl = *m_Impl;
        impl.ready();
        auto& buffer = resource(impl.buffers, id);
        if (data.size() != buffer.storage.desc.size)
        {
            throw std::invalid_argument("Compute upload byte count mismatch");
        }
        Buffer staging(impl.device, {data.size(), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto*  mapped = impl.device.core.MapBuffer(staging.handle, 0, data.size());
        if (!mapped)
        {
            throw std::runtime_error("Map compute upload failed");
        }
        std::memcpy(mapped, data.data(), data.size());
        impl.device.core.UnmapBuffer(staging.handle);
        auto* cmd = impl.frame.begin();
        buffer.storage.transition(cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        const VriBufferCopyDesc copy {0, 0, data.size()};
        impl.device.core.CmdCopyBuffer(cmd, buffer.storage.handle, staging.handle, &copy);
        impl.complete();
        buffer.initialized = true;
    }

    void ComputeContext::readBuffer(uint64_t id, std::span<std::byte> data)
    {
        auto& impl = *m_Impl;
        impl.ready();
        auto& buffer = resource(impl.buffers, id);
        if (!buffer.initialized || data.size() != buffer.storage.desc.size)
        {
            throw std::invalid_argument("Compute readback needs initialized storage and an exact byte count");
        }
        Buffer staging(impl.device, {data.size(), 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto*  cmd = impl.frame.begin();
        buffer.storage.transition(cmd, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        staging.transition(cmd, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        const VriBufferCopyDesc copy {0, 0, data.size()};
        impl.device.core.CmdCopyBuffer(cmd, staging.handle, buffer.storage.handle, &copy);
        impl.complete();
        auto* mapped = impl.device.core.MapBuffer(staging.handle, 0, data.size());
        if (!mapped)
        {
            throw std::runtime_error("Map compute readback failed");
        }
        std::memcpy(data.data(), mapped, data.size());
        impl.device.core.UnmapBuffer(staging.handle);
    }

    uint64_t ComputeContext::loadKernel(std::string_view path, std::string_view entry)
    {
        auto& impl = *m_Impl;
        impl.ready();
        if (path.empty() || entry.empty() || path.find('\0') != std::string_view::npos ||
            entry.find('\0') != std::string_view::npos)
        {
            throw std::invalid_argument("Compute kernel path and entry must be nonempty and contain no NUL");
        }
        const std::array entries {ShaderEntry {std::string(entry), VriShaderStage_Compute}};
        auto             program = ShaderProgram::compile(std::filesystem::u8path(path), entries);
        const auto       id      = identity(true);
        impl.kernels.emplace(id, std::make_unique<ComputeKernel>(impl.device, std::move(program)));
        return id;
    }

    std::string ComputeContext::kernelLayout(uint64_t id) const
    {
        m_Impl->ready();
        return resource(m_Impl->kernels, id).metadata.dump();
    }

    void ComputeContext::dispatch(uint64_t                   id,
                                  std::span<const uint64_t>  ids,
                                  std::span<const std::byte> constants,
                                  uint32_t                   x,
                                  uint32_t                   y,
                                  uint32_t                   z)
    {
        auto& impl = *m_Impl;
        impl.ready();
        auto& kernel = resource(impl.kernels, id);
        if (!x || !y || !z || ids.size() != kernel.buffers.size() || constants.size() != kernel.constantsSize)
        {
            throw std::invalid_argument("Compute dispatch dimensions, buffer count or constant size mismatch");
        }
        std::vector<ComputeBuffer*>       buffers;
        std::vector<const VriDescriptor*> views;
        std::set<uint64_t>                unique;
        for (size_t index = 0; index < ids.size(); ++index)
        {
            auto&      buffer   = resource(impl.buffers, ids[index]);
            const bool writable = kernel.buffers[index].type == VriDescriptorType_StorageBuffer;
            if (!unique.insert(ids[index]).second || buffer.scalar != kernel.scalars[index] ||
                (!writable && !buffer.initialized))
            {
                throw std::invalid_argument("Compute input is aliased, uninitialized or has the wrong scalar type: " +
                                            kernel.buffers[index].name);
            }
            buffers.push_back(&buffer);
            views.push_back(buffer.views[writable ? 1 : 0]);
        }
        std::vector<VriDescriptorRangeUpdateDesc> updates(views.size());
        for (size_t index = 0; index < views.size(); ++index)
        {
            updates[index].descriptors   = &views[index];
            updates[index].descriptorNum = 1;
        }
        impl.device.core.UpdateDescriptorRanges(kernel.set, 0, uint32_t(updates.size()), updates.data());
        auto* cmd = impl.frame.begin();
        for (size_t index = 0; index < buffers.size(); ++index)
        {
            buffers[index]->storage.transition(
                cmd,
                {kernel.buffers[index].type == VriDescriptorType_StorageBuffer ?
                     VriAccessFlags(VriAccess_ShaderResourceStorageRead | VriAccess_ShaderResourceStorageWrite) :
                     VriAccessFlags(VriAccess_ShaderResourceRead),
                 VriPipelineStage_ComputeShader});
        }
        impl.device.core.CmdSetPipelineLayout(cmd, kernel.layout);
        impl.device.core.CmdSetPipeline(cmd, kernel.pipeline);
        impl.device.core.CmdSetDescriptorSet(cmd, 0, kernel.set);
        if (!constants.empty())
        {
            impl.device.core.CmdSetConstants(cmd, 0, constants.data(), uint32_t(constants.size()));
        }
        const VriDispatchDesc desc {x, y, z};
        impl.device.core.CmdDispatch(cmd, &desc);
        impl.complete();
        for (size_t index = 0; index < buffers.size(); ++index)
        {
            if (kernel.buffers[index].type == VriDescriptorType_StorageBuffer)
            {
                buffers[index]->initialized = true;
            }
        }
    }

    void ComputeContext::releaseBuffer(uint64_t id)
    {
        m_Impl->ready();
        resource(m_Impl->buffers, id);
        m_Impl->buffers.erase(id);
    }

    void ComputeContext::releaseKernel(uint64_t id)
    {
        m_Impl->ready();
        resource(m_Impl->kernels, id);
        m_Impl->kernels.erase(id);
    }
} // namespace vultra
