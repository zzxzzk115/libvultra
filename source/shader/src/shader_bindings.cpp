#include <vultra/shader/shader_program.hpp>

#include <algorithm>
#include <array>

namespace vultra
{
    namespace
    {
        bool hasOffset(const ShaderParameter& node, ShaderOffsetKind kind)
        {
            return std::ranges::any_of(node.offsets,
                                       [kind](const auto& offset)
                                       {
                                           return offset.kind == kind;
                                       });
        }

        // D3D targets number registers per class (b, t, u, s), each in its own register space. Other targets
        // report descriptor slots instead and leave these at zero.
        constexpr std::array kRegisterKinds {ShaderOffsetKind::eConstantBufferRegister,
                                             ShaderOffsetKind::eShaderResourceRegister,
                                             ShaderOffsetKind::eUnorderedAccessRegister,
                                             ShaderOffsetKind::eSamplerRegister};

        struct Registers
        {
            std::array<uint64_t, kRegisterKinds.size()> index {};
            std::array<uint64_t, kRegisterKinds.size()> space {};

            void add(const ShaderParameter& node)
            {
                for (size_t kind = 0; kind < kRegisterKinds.size(); ++kind)
                {
                    index[kind] += node.offset(kRegisterKinds[kind]);
                    space[kind] += node.space(kRegisterKinds[kind]);
                }
            }
        };

        size_t registerClass(VriDescriptorType type)
        {
            switch (type)
            {
                case VriDescriptorType_ConstantBuffer:
                    return 0;
                case VriDescriptorType_StorageTexture:
                case VriDescriptorType_StorageBuffer:
                    return 2;
                case VriDescriptorType_Sampler:
                    return 3;
                default:
                    return 1;
            }
        }

        std::expected<void, Error> collectBindings(const ShaderParameter&              node,
                                                   std::string                         name,
                                                   uint64_t                            set,
                                                   uint64_t                            binding,
                                                   Registers                           registers,
                                                   uint64_t                            count,
                                                   std::vector<ShaderResourceBinding>& output)
        {
            if (node.kind == ShaderParameterKind::eParameterBlock)
            {
                set += node.offset(ShaderOffsetKind::eRegisterSpace);
                binding   = 0; // Bindings are relative to the deepest ParameterBlock.
                registers = {};
            }
            else
            {
                binding += node.offset(ShaderOffsetKind::eDescriptor);
                set += node.space(ShaderOffsetKind::eDescriptor);
                registers.add(node);
            }
            if (node.kind == ShaderParameterKind::eConstantBuffer || node.kind == ShaderParameterKind::eParameterBlock)
            {
                const auto* element   = node.field("$element");
                const auto* container = node.field("$container");
                if (!element || !container)
                {
                    return makeError("Missing reflected container/element: {}", name);
                }
                if (element->size)
                {
                    const bool push = hasOffset(node, ShaderOffsetKind::ePushConstant) ||
                                      hasOffset(*container, ShaderOffsetKind::ePushConstant);
                    const uint64_t slot = binding + container->offset(ShaderOffsetKind::eDescriptor) +
                                          registers.index[0] +
                                          container->offset(ShaderOffsetKind::eConstantBufferRegister);
                    const uint64_t space = set + registers.space[0];
                    if (space > UINT32_MAX || slot > UINT32_MAX || count > UINT32_MAX)
                    {
                        return makeError("Shader binding exceeds VRI limits: {}", name);
                    }
                    output.push_back({name,
                                      VriDescriptorType_ConstantBuffer,
                                      uint32_t(space),
                                      uint32_t(slot),
                                      uint32_t(count),
                                      element->size,
                                      VriTextureViewType_2D,
                                      push});
                }
                return collectBindings(*element, std::move(name), set, binding, registers, count, output);
            }
            else if (node.kind == ShaderParameterKind::eArray)
            {
                if (!node.count || node.count > 65536 || count > 65536 / node.count || node.fields.size() != 1)
                {
                    return makeError("Material passes require bounded resource arrays: {}", name);
                }
                return collectBindings(node.fields.front(),
                                       std::move(name),
                                       set,
                                       binding,
                                       registers,
                                       count * node.count,
                                       output);
            }
            else if (node.kind == ShaderParameterKind::eStruct)
            {
                for (const auto& field : node.fields)
                {
                    auto child = name.empty() ? field.name : name + "." + field.name;
                    if (auto collected =
                            collectBindings(field, std::move(child), set, binding, registers, count, output);
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
                        return makeError("Unsupported reflected material resource: {} ({})", name, node.typeName);
                    }
                    type = *node.descriptorType;
                }
                const auto     registerKind = registerClass(type);
                const uint64_t slot         = binding + registers.index[registerKind];
                const uint64_t space        = set + registers.space[registerKind];
                if (space > UINT32_MAX || slot > UINT32_MAX || count > 65536)
                {
                    return makeError("Shader binding exceeds VRI limits: {}", name);
                }
                output.push_back({std::move(name),
                                  type,
                                  uint32_t(space),
                                  uint32_t(slot),
                                  uint32_t(count),
                                  0,
                                  node.viewType,
                                  false,
                                  node.storageFormat,
                                  node.writeOnly});
            }
            return {};
        }
    } // namespace

    std::expected<std::vector<ShaderResourceBinding>, Error> ShaderProgram::resourceBindings() const
    {
        std::vector<ShaderResourceBinding> result;
        if (auto collected = collectBindings(parameters, "", 0, 0, {}, 1, result); !collected)
        {
            return std::unexpected(collected.error());
        }
        return result;
    }

} // namespace vultra
