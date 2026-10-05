#include "shader_archive.hpp"

#include <slang.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace vultra
{
    uint64_t ShaderParameter::offset(ShaderOffsetKind kind) const
    {
        for (const auto& value : offsets)
        {
            if (value.kind == kind)
            {
                return value.offset;
            }
        }
        return 0;
    }

    uint64_t ShaderParameter::space(ShaderOffsetKind kind) const
    {
        for (const auto& value : offsets)
        {
            if (value.kind == kind)
            {
                return value.space;
            }
        }
        return 0;
    }

    const ShaderParameter* ShaderParameter::field(std::string_view requested) const
    {
        for (const auto& child : fields)
        {
            if (child.name == requested)
            {
                return &child;
            }
        }
        for (const auto& child : fields)
        {
            if (child.name == "$element" || child.name == "$container")
            {
                if (auto* found = child.field(requested))
                {
                    return found;
                }
            }
        }
        return nullptr;
    }

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
                                      0,
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
                    const auto shape    = node.resourceShape & SLANG_RESOURCE_BASE_SHAPE_MASK;
                    const bool writable = node.resourceAccess != SLANG_RESOURCE_ACCESS_READ;
                    if (shape == SLANG_STRUCTURED_BUFFER || shape == SLANG_BYTE_ADDRESS_BUFFER)
                    {
                        type = writable ? VriDescriptorType_StorageBuffer : VriDescriptorType_StructuredBuffer;
                    }
                    else if (shape == SLANG_ACCELERATION_STRUCTURE)
                    {
                        type = VriDescriptorType_AccelerationStructure;
                    }
                    else if (shape >= SLANG_TEXTURE_1D && shape <= SLANG_TEXTURE_CUBE)
                    {
                        type = writable ? VriDescriptorType_StorageTexture : VriDescriptorType_Texture;
                    }
                    else
                    {
                        throw std::invalid_argument("Unsupported reflected material resource shape: " + name);
                    }
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
                                  node.resourceShape,
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

    namespace detail
    {
        namespace
        {
            ShaderParameterKind parameterKind(slang::TypeReflection::Kind kind)
            {
                using Kind = slang::TypeReflection::Kind;
                switch (kind)
                {
                    case Kind::Scalar:
                        return ShaderParameterKind::eScalar;
                    case Kind::Vector:
                        return ShaderParameterKind::eVector;
                    case Kind::Matrix:
                        return ShaderParameterKind::eMatrix;
                    case Kind::Struct:
                        return ShaderParameterKind::eStruct;
                    case Kind::Array:
                        return ShaderParameterKind::eArray;
                    case Kind::ConstantBuffer:
                        return ShaderParameterKind::eConstantBuffer;
                    case Kind::ParameterBlock:
                        return ShaderParameterKind::eParameterBlock;
                    case Kind::Resource:
                        return ShaderParameterKind::eResource;
                    case Kind::SamplerState:
                        return ShaderParameterKind::eSampler;
                    default:
                        return ShaderParameterKind::eNone;
                }
            }

            ShaderScalarType scalarType(slang::TypeReflection::ScalarType type)
            {
                using Type = slang::TypeReflection;
                switch (type)
                {
                    case Type::None:
                        return ShaderScalarType::eNone;
                    case Type::Float32:
                        return ShaderScalarType::eFloat;
                    case Type::Int32:
                        return ShaderScalarType::eInteger;
                    case Type::UInt32:
                        return ShaderScalarType::eUnsigned;
                    case Type::Bool:
                        return ShaderScalarType::eBoolean;
                    default:
                        return ShaderScalarType::eOther;
                }
            }

            ShaderParameter reflectType(slang::TypeLayoutReflection*     type,
                                        slang::VariableLayoutReflection* variable,
                                        std::string                      name,
                                        uint32_t                         depth)
            {
                if (!type || depth > 64)
                {
                    throw std::invalid_argument("Invalid or excessively nested Slang parameter layout");
                }
                ShaderParameter result;
                result.name = std::move(name);
                if (const auto* typeName = type->getName())
                {
                    result.typeName = typeName;
                }
                result.kind        = parameterKind(type->getKind());
                result.scalar      = scalarType(type->getScalarType());
                result.size        = type->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
                result.stride      = type->getStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
                result.rows        = type->getRowCount();
                result.columns     = type->getColumnCount();
                result.columnMajor = type->getMatrixLayoutMode() != SLANG_MATRIX_LAYOUT_ROW_MAJOR;
                if (result.kind == ShaderParameterKind::eMatrix)
                {
                    // Target layout includes the padding of each major vector; divide its full reflected span.
                    const auto vectors = result.columnMajor ? result.columns : result.rows;
                    if (!vectors || result.stride % vectors != 0)
                    {
                        throw std::runtime_error("Slang matrix layout has no integral major-vector stride");
                    }
                    result.matrixStride = result.stride / vectors;
                }
                result.resourceShape  = uint32_t(type->getResourceShape());
                result.resourceAccess = uint32_t(type->getResourceAccess());
                constexpr std::array categories {
                    std::pair {SLANG_PARAMETER_CATEGORY_UNIFORM, ShaderOffsetKind::eUniform},
                    std::pair {SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT, ShaderOffsetKind::eDescriptor},
                    std::pair {SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE, ShaderOffsetKind::eRegisterSpace},
                    std::pair {SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER, ShaderOffsetKind::ePushConstant}};
                for (uint32_t index = 0; index < type->getCategoryCount(); ++index)
                {
                    for (const auto& [category, kind] : categories)
                    {
                        if (uint32_t(type->getCategoryByIndex(index)) == uint32_t(category))
                        {
                            result.offsets.push_back({kind,
                                                      variable ? variable->getOffset(category) : 0,
                                                      variable ? variable->getBindingSpace(category) : 0});
                        }
                    }
                }
                if (result.name == "$container")
                {
                    return result;
                }
                if (result.kind == ShaderParameterKind::eStruct)
                {
                    for (uint32_t index = 0; index < type->getFieldCount(); ++index)
                    {
                        auto* field = type->getFieldByIndex(index);
                        result.fields.push_back(
                            reflectType(field->getTypeLayout(), field, field->getName(), depth + 1));
                    }
                }
                else if (result.kind == ShaderParameterKind::eArray)
                {
                    result.count  = type->getElementCount();
                    result.stride = type->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
                    auto* element = type->getElementVarLayout();
                    result.fields.push_back(
                        reflectType(element ? element->getTypeLayout() : type->getElementTypeLayout(),
                                    element,
                                    "$element",
                                    depth + 1));
                }
                else if (result.kind == ShaderParameterKind::eConstantBuffer ||
                         result.kind == ShaderParameterKind::eParameterBlock)
                {
                    if (auto* container = type->getContainerVarLayout())
                    {
                        result.fields.push_back(
                            reflectType(container->getTypeLayout(), container, "$container", depth + 1));
                    }
                    auto* element = type->getElementVarLayout();
                    result.fields.push_back(
                        reflectType(element ? element->getTypeLayout() : type->getElementTypeLayout(),
                                    element,
                                    "$element",
                                    depth + 1));
                }
                return result;
            }
        } // namespace

        ShaderParameter reflectShaderParameters(slang::VariableLayoutReflection* globals)
        {
            return reflectType(globals->getTypeLayout(), globals, "$globals", 0);
        }
    } // namespace detail
} // namespace vultra
