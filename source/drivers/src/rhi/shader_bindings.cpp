#include "shader_archive.hpp"

#include <vultra/drivers/rhi/device.hpp>
#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/drivers/rhi/shader_bindings.hpp>
#include <vultra/drivers/rhi/shader_uniform_block.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        constexpr size_t kMaximumOwnedUniforms = 65536;

        std::expected<VriShaderStageFlags, std::string> selectedStagesExpected(const ShaderProgram&         program,
                                                                               std::span<const ShaderEntry> entries)
        {
            if (entries.empty() || entries.size() > program.shaders.size())
            {
                return std::unexpected("Select a bounded nonempty set of shader entries");
            }
            if (auto selected = program.descriptorsExpected(entries); !selected)
            {
                return std::unexpected(selected.error());
            }
            VriShaderStageFlags        stages = 0;
            std::set<std::string_view> names;
            for (const auto& entry : entries)
            {
                if (!names.insert(entry.name).second)
                {
                    return std::unexpected("Duplicate shader entry: " + entry.name);
                }
                stages |= entry.stage;
            }
            return stages;
        }

        bool hasOffset(const ShaderParameter& node, ShaderOffsetKind kind)
        {
            return std::ranges::any_of(node.offsets,
                                       [kind](const auto& offset)
                                       {
                                           return offset.kind == kind;
                                       });
        }

        const ShaderParameter* uniformLayout(const ShaderParameter& node, std::string_view name)
        {
            if (name.ends_with(']'))
            {
                name = name.substr(0, name.rfind('['));
            }
            auto binding = node.bindingExpected(name);
            return binding ? (*binding)->field("$element") : nullptr;
        }

        std::string uniformElementName(const ShaderResourceBinding& binding, uint32_t index)
        {
            return binding.array ? binding.name + "[" + std::to_string(index) + "]" : binding.name;
        }

        std::expected<void, std::string> collectBindings(const ShaderParameter&              node,
                                                         std::string                         name,
                                                         uint64_t                            set,
                                                         uint64_t                            binding,
                                                         uint64_t                            count,
                                                         bool                                array,
                                                         uint32_t                            depth,
                                                         uint32_t&                           nodes,
                                                         std::vector<ShaderResourceBinding>& output)
        {
            if (depth > 64 || ++nodes > 100000)
            {
                return std::unexpected("Invalid or excessively large reflected shader bindings");
            }
            if (node.kind == ShaderParameterKind::eParameterBlock)
            {
                const auto offset = node.offset(ShaderOffsetKind::eRegisterSpace);
                if (offset > UINT32_MAX || set > UINT32_MAX - offset)
                {
                    return std::unexpected("Shader binding exceeds VRI limits: " + name);
                }
                set += offset;
                binding = 0; // Bindings are relative to the deepest ParameterBlock.
            }
            else
            {
                const auto offset = node.offset(ShaderOffsetKind::eDescriptor);
                const auto space  = node.space(ShaderOffsetKind::eDescriptor);
                if (offset > UINT32_MAX || binding > UINT32_MAX - offset || space > UINT32_MAX ||
                    set > UINT32_MAX - space)
                {
                    return std::unexpected("Shader binding exceeds VRI limits: " + name);
                }
                binding += offset;
                set += space;
            }
            if (node.kind == ShaderParameterKind::eConstantBuffer || node.kind == ShaderParameterKind::eParameterBlock)
            {
                const auto* element   = node.field("$element");
                const auto* container = node.field("$container");
                if (!element || !container)
                {
                    return std::unexpected("Missing reflected container/element: " + name);
                }
                if (element->size)
                {
                    const auto offset = container->offset(ShaderOffsetKind::eDescriptor);
                    if (offset > UINT32_MAX || binding > UINT32_MAX - offset || count > UINT32_MAX)
                    {
                        return std::unexpected("Shader binding exceeds VRI limits: " + name);
                    }
                    const bool push = hasOffset(node, ShaderOffsetKind::ePushConstant) ||
                                      hasOffset(*container, ShaderOffsetKind::ePushConstant);
                    output.push_back({name,
                                      VriDescriptorType_ConstantBuffer,
                                      uint32_t(set),
                                      uint32_t(binding + offset),
                                      uint32_t(count),
                                      element->size,
                                      VriTextureViewType_2D,
                                      push,
                                      array});
                }
                return collectBindings(*element, std::move(name), set, binding, count, array, depth + 1, nodes, output);
            }
            if (node.kind == ShaderParameterKind::eArray)
            {
                if (!node.count || node.count > 65536 || count > 65536 / node.count || node.fields.size() != 1)
                {
                    return std::unexpected("Material passes require bounded resource arrays: " + name);
                }
                return collectBindings(node.fields.front(),
                                       std::move(name),
                                       set,
                                       binding,
                                       count * node.count,
                                       true,
                                       depth + 1,
                                       nodes,
                                       output);
            }
            if (node.kind == ShaderParameterKind::eStruct)
            {
                for (const auto& field : node.fields)
                {
                    auto child = name.empty() ? field.name : name + "." + field.name;
                    if (auto collected = collectBindings(field,
                                                         std::move(child),
                                                         set,
                                                         binding,
                                                         count,
                                                         array,
                                                         depth + 1,
                                                         nodes,
                                                         output);
                        !collected)
                    {
                        return collected;
                    }
                }
            }
            else if (node.kind == ShaderParameterKind::eResource || node.kind == ShaderParameterKind::eSampler)
            {
                VriDescriptorType type = VriDescriptorType_Sampler;
                if (node.kind == ShaderParameterKind::eResource)
                {
                    if (!node.descriptorType)
                    {
                        return std::unexpected("Unsupported reflected material resource: " + name + " (" +
                                               node.typeName + ")");
                    }
                    type = *node.descriptorType;
                }
                if (count > 65536 || !count)
                {
                    return std::unexpected("Shader binding exceeds VRI limits: " + name);
                }
                output.push_back({std::move(name),
                                  type,
                                  uint32_t(set),
                                  uint32_t(binding),
                                  uint32_t(count),
                                  0,
                                  node.viewType,
                                  false,
                                  array});
            }
            return {};
        }
    } // namespace

    std::vector<ShaderResourceBinding> ShaderProgram::resourceBindings() const
    {
        auto bindings = resourceBindingsExpected();
        if (!bindings)
        {
            throw std::invalid_argument(bindings.error());
        }
        return std::move(*bindings);
    }

    std::expected<std::vector<ShaderResourceBinding>, std::string> ShaderProgram::resourceBindingsExpected() const
    {
        std::vector<ShaderResourceBinding> result;
        uint32_t                           nodes = 0;
        if (auto collected = collectBindings(parameters, "", 0, 0, 1, false, 0, nodes, result); !collected)
        {
            return std::unexpected(collected.error());
        }
        return result;
    }

    ShaderUniformUpload::ShaderUniformUpload(std::string_view name, std::span<const std::byte> bytes) :
        name(name),
        bytes(bytes)
    {
    }

    ShaderUniformUpload::ShaderUniformUpload(std::string_view name, const ShaderUniformBlock& block) :
        name(name),
        bytes(block.data()),
        m_Layout(&block.layout())
    {
    }

    struct ShaderBindings::State
    {
        struct Uniform
        {
            Device&                 device;
            uint64_t                size;
            std::unique_ptr<Buffer> buffer;
            VriDescriptor*          view = nullptr;
            std::vector<std::byte>  accepted;
            const ShaderParameter*  layout = nullptr;

            Uniform(Device& device, uint64_t size) :
                device(device),
                size(size)
            {
            }

            ~Uniform()
            {
                if (view)
                {
                    device.core.DestroyDescriptor(view);
                }
            }
        };

        Device&                            device;
        VriPipelineLayout*                 layout = nullptr;
        VriDescriptorPool*                 pool   = nullptr;
        VriDescriptorPoolDesc                       poolDesc {};
        bool                                        borrowedLayout = false;
        std::vector<VriDescriptorSet*>     sets;
        std::vector<ShaderResourceBinding> bindings;
        std::vector<uint32_t>              ranges;
        ShaderParameter                    parameters;
        std::map<std::string, Uniform, std::less<>> uniforms;
        std::map<std::string, std::vector<const VriDescriptor*>, std::less<>> uniformViews;
        VriShaderStageFlags                stages   = 0;
        uint32_t                           pushSize = 0;
        size_t                             required = 0;
        bool                               ready    = false;

        explicit State(Device& device) :
            device(device)
        {
        }

        std::expected<void, std::string> allocateStorageExpected()
        {
            if (poolDesc.descriptorSetMaxNum)
            {
                const auto result = device.core.CreateDescriptorPool(device.handle, &poolDesc, &pool);
                if (result != VriResult_Success || !pool)
                {
                    return std::unexpected("Create reflected shader descriptor pool failed: " +
                                           std::to_string(uint32_t(result)));
                }
                sets.resize(poolDesc.descriptorSetMaxNum);
                for (uint32_t index = 0; index < sets.size(); ++index)
                {
                    const auto allocated = device.core.AllocateDescriptorSets(pool, layout, index, &sets[index], 1);
                    if (allocated != VriResult_Success || !sets[index])
                    {
                        return std::unexpected("Allocate reflected shader descriptor set failed: " +
                                               std::to_string(uint32_t(allocated)));
                    }
                }
            }
            for (auto& [name, uniform] : uniforms)
            {
                auto buffer =
                    Buffer::create(device,
                                   {uniform.size, 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
                if (!buffer)
                {
                    return std::unexpected("Create owned uniform " + name + ": " + buffer.error());
                }
                uniform.buffer = std::move(*buffer);
                const VriBufferViewDesc view {uniform.buffer->handle,
                                              VriDescriptorType_ConstantBuffer,
                                              VriFormat_Unknown,
                                              0,
                                              uniform.size};
                const auto              result = device.core.CreateBufferView(device.handle, &view, &uniform.view);
                if (result != VriResult_Success || !uniform.view)
                {
                    return std::unexpected("Create owned uniform view " + name + ": " +
                                           std::to_string(uint32_t(result)));
                }
            }
            for (const auto& binding : bindings)
            {
                const auto owned = uniformViews.find(binding.name);
                if (owned != uniformViews.end())
                {
                    for (uint32_t index = 0; index < binding.count; ++index)
                    {
                        owned->second[index] = uniforms.find(uniformElementName(binding, index))->second.view;
                    }
                }
            }
            ready = required == 0;
            return {};
        }

        ~State()
        {
            if (pool)
            {
                device.core.DestroyDescriptorPool(pool);
            }
            if (layout && !borrowedLayout)
            {
                device.core.DestroyPipelineLayout(layout);
            }
        }
    };

    ShaderBindings::ShaderBindings(std::unique_ptr<State> state) :
        m_State(std::move(state))
    {
    }

    ShaderBindings::~ShaderBindings() = default;

    std::expected<std::unique_ptr<ShaderBindings>, std::string>
    ShaderBindings::create(Device&                           device,
                           const ShaderProgram&              program,
                           std::span<const ShaderEntry>      entries,
                           std::span<const std::string_view> uniformBlocks)
    {
        if (auto valid = detail::validateShaderProgramExpected(program, *device.core.GetDeviceDesc(device.handle));
            !valid)
        {
            return std::unexpected(valid.error());
        }
        auto selected = selectedStagesExpected(program, entries);
        if (!selected)
        {
            return std::unexpected(selected.error());
        }
        const auto stages    = *selected;
        auto       reflected = program.resourceBindingsExpected();
        if (!reflected)
        {
            return std::unexpected(reflected.error());
        }
        auto state        = std::make_unique<State>(device);
        state->bindings   = std::move(*reflected);
        state->parameters = program.parameters;
        state->stages     = stages;
        state->ranges.resize(state->bindings.size());
        for (const auto name : uniformBlocks)
        {
            auto     found   = std::ranges::find(state->bindings, name, &ShaderResourceBinding::name);
            uint32_t first   = 0;
            bool     element = false;
            if (found == state->bindings.end() && name.ends_with(']'))
            {
                const auto bracket = name.rfind('[');
                if (bracket != std::string_view::npos)
                {
                    const auto digits = name.substr(bracket + 1, name.size() - bracket - 2);
                    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), first);
                    if (parsed.ec == std::errc {} && parsed.ptr == digits.data() + digits.size() &&
                        digits == std::to_string(first))
                    {
                        found =
                            std::ranges::find(state->bindings, name.substr(0, bracket), &ShaderResourceBinding::name);
                        element = true;
                    }
                }
            }
            if (found == state->bindings.end() || found->pushConstant ||
                found->type != VriDescriptorType_ConstantBuffer || !found->count || !found->uniformSize ||
                (element && (!found->array || first >= found->count)))
            {
                return std::unexpected("Owned uniform requires a reflected constant buffer or array element: " +
                                       std::string(name));
            }
            auto layout = uniformLayout(state->parameters, found->name);
            if (!layout || layout->size != found->uniformSize)
            {
                return std::unexpected("Missing reflected owned uniform layout: " + std::string(name));
            }
            state->uniformViews.try_emplace(found->name);
            const auto end = element ? first + 1 : found->count;
            if (end - first > kMaximumOwnedUniforms - state->uniforms.size())
            {
                return std::unexpected("Owned uniform count exceeds supported ownership");
            }
            for (uint32_t index = first; index < end; ++index)
            {
                const auto key           = uniformElementName(*found, index);
                auto [uniform, inserted] = state->uniforms.try_emplace(key, device, found->uniformSize);
                if (!inserted)
                {
                    return std::unexpected("Duplicate owned uniform block: " + key);
                }
                uniform->second.layout = layout;
            }
        }
        for (const auto& binding : state->bindings)
        {
            const auto owned = state->uniformViews.find(binding.name);
            if (owned != state->uniformViews.end())
            {
                for (uint32_t index = 0; index < binding.count; ++index)
                {
                    if (!state->uniforms.contains(uniformElementName(binding, index)))
                    {
                        return std::unexpected("Owned uniform arrays require every element: " + binding.name);
                    }
                }
                owned->second.resize(binding.count);
            }
        }
        std::map<uint32_t, std::vector<VriDescriptorRangeDesc>> groups;
        std::set<std::string_view>                              names;
        std::vector<VriPushConstantDesc>                        pushes;
        VriDescriptorPoolDesc                                   pool {};
        for (size_t i = 0; i < state->bindings.size(); ++i)
        {
            const auto& binding = state->bindings[i];
            if (!names.insert(binding.name).second)
            {
                return std::unexpected("Duplicate reflected shader binding name: " + binding.name);
            }
            if (binding.pushConstant)
            {
                if (binding.count != 1 || binding.array || binding.uniformSize > 128 || !pushes.empty())
                {
                    return std::unexpected("Shader bindings support one push constant block up to 128 bytes");
                }
                state->pushSize = uint32_t(binding.uniformSize);
                pushes.push_back({0, state->pushSize, stages});
                continue;
            }
            if (binding.set >= 32)
            {
                return std::unexpected("Reflected descriptor set index exceeds layout limit (31)");
            }
            auto& ranges = groups[binding.set];
            if (std::ranges::any_of(ranges,
                                    [&](const auto& range)
                                    {
                                        return range.baseRegister == binding.binding;
                                    }))
            {
                return std::unexpected("Overlapping reflected shader bindings: " + binding.name);
            }
            state->ranges[i] = uint32_t(ranges.size());
            ranges.push_back({binding.binding,
                              binding.count,
                              binding.type,
                              stages,
                              binding.array ? VriDescriptorRange_Array : VriDescriptorRangeFlags(0),
                              binding.viewType});
            uint32_t* count = nullptr;
            switch (binding.type)
            {
                case VriDescriptorType_Sampler:
                    count = &pool.samplerMaxNum;
                    break;
                case VriDescriptorType_Texture:
                    count = &pool.textureMaxNum;
                    break;
                case VriDescriptorType_StorageTexture:
                    count = &pool.storageTextureMaxNum;
                    break;
                case VriDescriptorType_ConstantBuffer:
                    count = &pool.constantBufferMaxNum;
                    break;
                case VriDescriptorType_StructuredBuffer:
                    count = &pool.structuredBufferMaxNum;
                    break;
                case VriDescriptorType_StorageBuffer:
                    count = &pool.storageBufferMaxNum;
                    break;
                case VriDescriptorType_AccelerationStructure:
                    count = &pool.accelerationStructureMaxNum;
                    break;
                default:
                    return std::unexpected("Unsupported reflected shader descriptor type: " + binding.name);
            }
            if (!binding.count || binding.count > UINT32_MAX - *count)
            {
                return std::unexpected("Reflected shader descriptor count exceeds VRI limits: " + binding.name);
            }
            *count += binding.count;
            ++state->required;
        }
        std::vector<VriDescriptorSetDesc> sets;
        if (!groups.empty())
        {
            const auto highest = groups.rbegin()->first;
            for (uint32_t space = 0; space <= highest; ++space)
            {
                const auto& ranges = groups[space];
                sets.push_back({space, ranges.data(), uint32_t(ranges.size())});
            }
        }
        VriPipelineLayoutDesc layout {sets.data(),
                                      uint32_t(sets.size()),
                                      pushes.data(),
                                      uint32_t(pushes.size()),
                                      stages};
        if (const auto result = device.core.CreatePipelineLayout(device.handle, &layout, &state->layout);
            result != VriResult_Success || !state->layout)
        {
            return std::unexpected("Create reflected shader layout failed: " + std::to_string(uint32_t(result)));
        }
        pool.descriptorSetMaxNum = uint32_t(sets.size());
        state->poolDesc          = pool;
        if (auto allocated = state->allocateStorageExpected(); !allocated)
        {
            return std::unexpected(allocated.error());
        }
        return std::unique_ptr<ShaderBindings>(new ShaderBindings(std::move(state)));
    }

    std::expected<std::unique_ptr<ShaderBindings>, std::string> ShaderBindings::createInstanceExpected() const
    {
        auto state            = std::make_unique<State>(m_State->device);
        state->layout         = m_State->layout;
        state->borrowedLayout = true;
        state->poolDesc       = m_State->poolDesc;
        state->parameters     = m_State->parameters;
        state->bindings       = m_State->bindings;
        state->ranges         = m_State->ranges;
        state->stages         = m_State->stages;
        state->pushSize       = m_State->pushSize;
        state->required       = m_State->required;
        for (const auto& [name, views] : m_State->uniformViews)
        {
            state->uniformViews.emplace(name, std::vector<const VriDescriptor*>(views.size()));
        }
        for (const auto& [name, uniform] : m_State->uniforms)
        {
            auto& copy  = state->uniforms.try_emplace(name, state->device, uniform.size).first->second;
            copy.layout = uniformLayout(state->parameters, name);
        }
        if (auto allocated = state->allocateStorageExpected(); !allocated)
        {
            return std::unexpected(allocated.error());
        }
        return std::unique_ptr<ShaderBindings>(new ShaderBindings(std::move(state)));
    }

    VriPipelineLayout* ShaderBindings::layout() const
    {
        return m_State->layout;
    }

    std::span<const ShaderResourceBinding> ShaderBindings::bindings() const
    {
        return m_State->bindings;
    }

    std::expected<Buffer*, std::string> ShaderBindings::uniformBufferExpected(std::string_view name)
    {
        const auto found = m_State->uniforms.find(name);
        if (found == m_State->uniforms.end())
        {
            return std::unexpected("Shader bindings do not own the uniform buffer: " + std::string(name));
        }
        return found->second.buffer.get();
    }

    std::expected<ShaderUniformBlock, std::string> ShaderBindings::uniformBlockExpected(std::string_view name) const
    {
        const auto found = m_State->uniforms.find(name);
        if (found == m_State->uniforms.end() || found->second.accepted.empty())
        {
            return std::unexpected("Owned uniform has no accepted CPU values: " + std::string(name));
        }
        return ShaderUniformBlock::create(*found->second.layout, found->second.accepted);
    }

    std::expected<std::unique_ptr<ShaderBindings>, std::string>
    ShaderBindings::reconcileExpected(const ShaderProgram&                 program,
                                      std::span<const ShaderEntry>         entries,
                                      std::span<const std::string_view>    uniformBlocks,
                                      std::span<const ShaderResourceViews> resources,
                                      std::string&                         diagnostics) const
    {
        if (!m_State->ready)
        {
            return std::unexpected("Prepare source shader bindings before reconciling accepted values");
        }
        auto candidate = create(m_State->device, program, entries, uniformBlocks);
        if (!candidate)
        {
            return std::unexpected(candidate.error());
        }
        std::map<std::string, ShaderUniformBlock, std::less<>> values;
        std::string                                            messages;
        for (const auto& [name, uniform] : (*candidate)->m_State->uniforms)
        {
            const bool existing = m_State->uniforms.contains(name);
            auto       block    = existing ? uniformBlockExpected(name) : ShaderUniformBlock::create(*uniform.layout);
            if (!block)
            {
                return std::unexpected("Prepare owned uniform " + name + ": " + block.error());
            }
            if (existing)
            {
                std::string fields;
                block = block->reconcileExpected(*uniform.layout, fields);
                if (!block)
                {
                    return std::unexpected("Reconcile owned uniform " + name + ": " + block.error());
                }
                if (!fields.empty())
                {
                    messages += "Uniform block: " + name + "\n" + fields;
                }
            }
            else
            {
                messages += "Initialized owned uniform: " + name + "\n";
            }
            values.emplace(name, std::move(*block));
        }
        for (const auto& [name, uniform] : m_State->uniforms)
        {
            if (!(*candidate)->m_State->uniforms.contains(name))
            {
                messages += "Released owned uniform: " + name + "\n";
            }
        }
        std::vector<ShaderUniformUpload> uploads;
        uploads.reserve(values.size());
        for (const auto& [name, block] : values)
        {
            uploads.emplace_back(name, block);
        }
        if (auto uploaded = (*candidate)->updateExpected(resources, uploads); !uploaded)
        {
            return std::unexpected(uploaded.error());
        }
        diagnostics += messages;
        return candidate;
    }

    uint32_t ShaderBindings::pushConstantSize() const
    {
        return m_State->pushSize;
    }

    std::expected<void, std::string> ShaderBindings::validateLayoutExpected(const ShaderProgram&         program,
                                                                            std::span<const ShaderEntry> entries) const
    {
        if (auto valid =
                detail::validateShaderProgramExpected(program,
                                                      *m_State->device.core.GetDeviceDesc(m_State->device.handle));
            !valid)
        {
            return valid;
        }
        auto selected = selectedStagesExpected(program, entries);
        if (!selected)
        {
            return std::unexpected(selected.error());
        }
        if (*selected != m_State->stages || program.parameters != m_State->parameters)
        {
            return std::unexpected("Reflected shader layout changed; prepare replacement bindings and uniform storage");
        }
        return {};
    }

    std::expected<void, std::string>
    ShaderBindings::validateExpected(std::span<const ShaderResourceViews> resources,
                                     std::span<const ShaderUniformUpload> uniforms) const
    {
        if (resources.size() != m_State->required - m_State->uniformViews.size())
        {
            return std::unexpected("Supply every borrowed non-push binding exactly once");
        }
        for (const auto& binding : m_State->bindings)
        {
            if (binding.pushConstant || m_State->uniformViews.contains(binding.name))
            {
                continue;
            }
            const auto found = std::ranges::find(resources, binding.name, &ShaderResourceViews::name);
            if (found == resources.end() ||
                std::ranges::count(resources, binding.name, &ShaderResourceViews::name) != 1)
            {
                return std::unexpected("Missing or duplicate reflected shader resource: " + binding.name);
            }
            if (found->type != binding.type || found->views.size() != binding.count ||
                std::ranges::any_of(found->views,
                                    [](const auto* view)
                                    {
                                        return view == nullptr;
                                    }))
            {
                return std::unexpected("Incompatible or null reflected shader resource: " + binding.name);
            }
        }
        for (const auto& upload : uniforms)
        {
            const auto found = m_State->uniforms.find(upload.name);
            if (found == m_State->uniforms.end() ||
                std::ranges::count(uniforms, upload.name, &ShaderUniformUpload::name) != 1 ||
                upload.bytes.size() != found->second.size)
            {
                return std::unexpected("Unknown, duplicate or incorrectly sized owned uniform: " +
                                       std::string(upload.name));
            }
            if (upload.m_Layout && *upload.m_Layout != *found->second.layout)
            {
                return std::unexpected("Typed uniform layout does not match owned binding: " +
                                       std::string(upload.name));
            }
        }
        for (const auto& [name, uniform] : m_State->uniforms)
        {
            if (uniform.accepted.empty() &&
                std::ranges::find(uniforms, name, &ShaderUniformUpload::name) == uniforms.end())
            {
                return std::unexpected("Supply the initial owned uniform bytes: " + name);
            }
        }
        return {};
    }

    std::expected<void, std::string> ShaderBindings::updateExpected(std::span<const ShaderResourceViews> resources,
                                                                    std::span<const ShaderUniformUpload> uniforms)
    {
        if (auto valid = validateExpected(resources, uniforms); !valid)
        {
            return valid;
        }

        struct Pending
        {
            State::Uniform*        uniform;
            std::vector<std::byte> bytes;
            void*                  mapped = nullptr;
        };

        std::vector<Pending> pending;
        for (const auto& upload : uniforms)
        {
            auto& uniform = m_State->uniforms.find(upload.name)->second;
            if (!std::ranges::equal(uniform.accepted, upload.bytes))
            {
                pending.push_back({&uniform, {upload.bytes.begin(), upload.bytes.end()}});
            }
        }
        auto& core = m_State->device.core;
        for (auto& change : pending)
        {
            change.mapped = core.MapBuffer(change.uniform->buffer->handle, 0, change.uniform->size);
            if (!change.mapped)
            {
                for (const auto& mapped : pending)
                {
                    if (mapped.mapped)
                    {
                        core.UnmapBuffer(mapped.uniform->buffer->handle);
                    }
                }
                return std::unexpected("Map owned shader uniform failed; accepted data and bindings retained");
            }
        }
        for (auto& change : pending)
        {
            std::memcpy(change.mapped, change.bytes.data(), change.bytes.size());
            core.UnmapBuffer(change.uniform->buffer->handle);
            change.uniform->accepted.swap(change.bytes);
        }
        for (size_t i = 0; i < m_State->bindings.size(); ++i)
        {
            const auto& binding = m_State->bindings[i];
            if (!binding.pushConstant)
            {
                const auto                                  owned = m_State->uniformViews.find(binding.name);
                const std::span<const VriDescriptor* const> views =
                    owned != m_State->uniformViews.end() ?
                        std::span(owned->second) :
                        std::span(std::ranges::find(resources, binding.name, &ShaderResourceViews::name)->views);
                const VriDescriptorRangeUpdateDesc update {views.data(), uint32_t(views.size()), 0};
                // Refresh even identical CPU addresses: a borrowed descriptor may have been recycled.
                m_State->device.core.UpdateDescriptorRanges(m_State->sets[binding.set], m_State->ranges[i], 1, &update);
            }
        }
        m_State->ready = true;
        return {};
    }

    std::expected<void, std::string> ShaderBindings::bindExpected(VriCommandBuffer* commands,
                                                                  VriPipeline*      pipeline) const
    {
        if (!commands || !pipeline)
        {
            return std::unexpected("Shader binding requires a command buffer and compatible pipeline");
        }
        if (!m_State->ready)
        {
            return std::unexpected("Prepare every reflected shader resource before binding");
        }
        auto& core = m_State->device.core;
        core.CmdSetPipelineLayout(commands, m_State->layout);
        core.CmdSetPipeline(commands, pipeline);
        for (uint32_t i = 0; i < m_State->sets.size(); ++i)
        {
            core.CmdSetDescriptorSet(commands, i, m_State->sets[i]);
        }
        return {};
    }
} // namespace vultra
