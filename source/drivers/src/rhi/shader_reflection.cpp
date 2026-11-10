#include "shader_archive.hpp"

#include <slang.h>

#include <array>

namespace vultra
{
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
                    case Type::Float16:
                        return ShaderScalarType::eFloat16;
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

            void reflectResource(slang::TypeLayoutReflection* type, ShaderParameter& result)
            {
                const auto shape    = type->getResourceShape();
                const auto base     = shape & SLANG_RESOURCE_BASE_SHAPE_MASK;
                const bool writable = type->getResourceAccess() != SLANG_RESOURCE_ACCESS_READ;
                if (base == SLANG_STRUCTURED_BUFFER || base == SLANG_BYTE_ADDRESS_BUFFER)
                {
                    result.descriptorType =
                        writable ? VriDescriptorType_StorageBuffer : VriDescriptorType_StructuredBuffer;
                }
                else if (base == SLANG_ACCELERATION_STRUCTURE)
                {
                    result.descriptorType = VriDescriptorType_AccelerationStructure;
                }
                else if (base >= SLANG_TEXTURE_1D && base <= SLANG_TEXTURE_CUBE)
                {
                    result.descriptorType = writable ? VriDescriptorType_StorageTexture : VriDescriptorType_Texture;
                    const bool array      = (shape & SLANG_TEXTURE_ARRAY_FLAG) != 0;
                    switch (base)
                    {
                        case SLANG_TEXTURE_1D:
                            result.viewType = array ? VriTextureViewType_1DArray : VriTextureViewType_1D;
                            break;
                        case SLANG_TEXTURE_2D:
                            result.viewType = array ? VriTextureViewType_2DArray : VriTextureViewType_2D;
                            break;
                        case SLANG_TEXTURE_3D:
                            result.viewType = VriTextureViewType_3D;
                            break;
                        case SLANG_TEXTURE_CUBE:
                            result.viewType = array ? VriTextureViewType_CubeArray : VriTextureViewType_Cube;
                            break;
                    }
                }
            }

            std::expected<ShaderParameter, std::string> reflectType(slang::TypeLayoutReflection*     type,
                                                                    slang::VariableLayoutReflection* variable,
                                                                    slang::ShaderReflection*         program,
                                                                    std::string                      name,
                                                                    uint32_t                         depth,
                                                                    uint32_t&                        nodes)
            {
                if (!type || depth > 64 || ++nodes > 100000)
                {
                    return std::unexpected("Invalid or excessively large Slang parameter layout");
                }
                ShaderParameter result;
                result.name = std::move(name);
                const auto append = [&](slang::TypeLayoutReflection*     childType,
                                        slang::VariableLayoutReflection* childVariable,
                                        std::string                      childName) -> std::expected<void, std::string>
                {
                    auto child = reflectType(childType, childVariable, program, std::move(childName), depth + 1, nodes);
                    if (!child)
                    {
                        return std::unexpected(child.error());
                    }
                    result.fields.push_back(std::move(*child));
                    return {};
                };
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
                        return std::unexpected("Slang matrix layout has no integral major-vector stride");
                    }
                    result.matrixStride = result.stride / vectors;
                }
                if (result.kind == ShaderParameterKind::eResource)
                {
                    reflectResource(type, result);
                    // Structured-buffer elements have a target layout distinct from their descriptor slot.
                    if ((type->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK) == SLANG_STRUCTURED_BUFFER)
                    {
                        if (auto child = append(type->getElementTypeLayout(), nullptr, "$element"); !child)
                        {
                            return std::unexpected(child.error());
                        }
                    }
                }
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
                        if (auto child = append(field->getTypeLayout(), field, field->getName()); !child)
                        {
                            return std::unexpected(child.error());
                        }
                    }
                }
                else if (result.kind == ShaderParameterKind::eArray)
                {
                    // The linked program folds expression-sized arrays; the context-free Slang query can throw.
                    result.count  = type->getElementCount(program);
                    result.stride = type->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
                    auto* element = type->getElementVarLayout();
                    if (auto child = append(element ? element->getTypeLayout() : type->getElementTypeLayout(),
                                            element,
                                            "$element");
                        !child)
                    {
                        return std::unexpected(child.error());
                    }
                }
                else if (result.kind == ShaderParameterKind::eConstantBuffer ||
                         result.kind == ShaderParameterKind::eParameterBlock)
                {
                    if (auto* container = type->getContainerVarLayout())
                    {
                        if (auto child = append(container->getTypeLayout(), container, "$container"); !child)
                        {
                            return std::unexpected(child.error());
                        }
                    }
                    auto* element = type->getElementVarLayout();
                    if (auto child = append(element ? element->getTypeLayout() : type->getElementTypeLayout(),
                                            element,
                                            "$element");
                        !child)
                    {
                        return std::unexpected(child.error());
                    }
                }
                return result;
            }
        } // namespace

        std::expected<ShaderParameter, std::string> reflectShaderParametersExpected(slang::ShaderReflection* program)
        {
            auto* globals = program ? program->getGlobalParamsVarLayout() : nullptr;
            if (!globals)
            {
                return std::unexpected("Slang global parameter layout is unavailable");
            }
            uint32_t nodes = 0;
            return reflectType(globals->getTypeLayout(), globals, program, "$globals", 0, nodes);
        }
    } // namespace detail
} // namespace vultra
