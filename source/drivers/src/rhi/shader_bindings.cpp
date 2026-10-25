#include <vultra/drivers/rhi/shader_program.hpp>

#include <algorithm>
#include <stdexcept>

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

        void collectBindings(const ShaderParameter&              node,
                             std::string                         name,
                             uint64_t                            set,
                             uint64_t                            binding,
                             uint64_t                            count,
                             std::vector<ShaderResourceBinding>& output)
        {
            if (node.kind == ShaderParameterKind::eParameterBlock)
            {
                set += node.offset(ShaderOffsetKind::eRegisterSpace);
                binding = 0; // Bindings are relative to the deepest ParameterBlock.
            }
            else
            {
                binding += node.offset(ShaderOffsetKind::eDescriptor);
                set += node.space(ShaderOffsetKind::eDescriptor);
            }
            if (node.kind == ShaderParameterKind::eConstantBuffer || node.kind == ShaderParameterKind::eParameterBlock)
            {
                const auto* element   = node.field("$element");
                const auto* container = node.field("$container");
                if (!element || !container)
                {
                    throw std::invalid_argument("Missing reflected container/element: " + name);
                }
                if (element->size)
                {
                    const bool push = hasOffset(node, ShaderOffsetKind::ePushConstant) ||
                                      hasOffset(*container, ShaderOffsetKind::ePushConstant);
                    if (set > UINT32_MAX || binding + container->offset(ShaderOffsetKind::eDescriptor) > UINT32_MAX ||
                        count > UINT32_MAX)
                    {
                        throw std::invalid_argument("Shader binding exceeds VRI limits: " + name);
                    }
                    output.push_back({name,
                                      VriDescriptorType_ConstantBuffer,
                                      uint32_t(set),
                                      uint32_t(binding + container->offset(ShaderOffsetKind::eDescriptor)),
                                      uint32_t(count),
                                      element->size,
                                      VriTextureViewType_2D,
                                      push});
                }
                collectBindings(*element, std::move(name), set, binding, count, output);
            }
            else if (node.kind == ShaderParameterKind::eArray)
            {
                if (!node.count || node.count > 65536 || count > 65536 / node.count || node.fields.size() != 1)
                {
                    throw std::invalid_argument("Material passes require bounded resource arrays: " + name);
                }
                collectBindings(node.fields.front(), std::move(name), set, binding, count * node.count, output);
            }
            else if (node.kind == ShaderParameterKind::eStruct)
            {
                for (const auto& field : node.fields)
                {
                    auto child = name.empty() ? field.name : name + "." + field.name;
                    collectBindings(field, std::move(child), set, binding, count, output);
                }
            }
            else if (node.kind == ShaderParameterKind::eResource || node.kind == ShaderParameterKind::eSampler)
            {
                VriDescriptorType type = VriDescriptorType_Sampler;
                if (node.kind == ShaderParameterKind::eResource)
                {
                    if (!node.descriptorType)
                    {
                        throw std::invalid_argument("Unsupported reflected material resource: " + name + " (" +
                                                    node.typeName + ")");
                    }
                    type = *node.descriptorType;
                }
                if (set > UINT32_MAX || binding > UINT32_MAX || count > 65536)
                {
                    throw std::invalid_argument("Shader binding exceeds VRI limits: " + name);
                }
                output.push_back({std::move(name),
                                  type,
                                  uint32_t(set),
                                  uint32_t(binding),
                                  uint32_t(count),
                                  0,
                                  node.viewType,
                                  false});
            }
        }
    } // namespace

    std::vector<ShaderResourceBinding> ShaderProgram::resourceBindings() const
    {
        std::vector<ShaderResourceBinding> result;
        collectBindings(parameters, "", 0, 0, 1, result);
        return result;
    }

} // namespace vultra
