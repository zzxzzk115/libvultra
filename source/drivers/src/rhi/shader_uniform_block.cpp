#include <vultra/drivers/rhi/shader_uniform_block.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <map>
#include <type_traits>

namespace vultra
{
    namespace
    {
        using Result                     = std::expected<void, std::string>;
        constexpr uint64_t kMaximumBytes = 16 * 1024 * 1024;

        uint32_t scalarSize(ShaderScalarType scalar)
        {
            switch (scalar)
            {
                case ShaderScalarType::eFloat16:
                    return 2;
                case ShaderScalarType::eFloat:
                case ShaderScalarType::eInteger:
                case ShaderScalarType::eUnsigned:
                case ShaderScalarType::eBoolean:
                    return 4;
                default:
                    return 0;
            }
        }

        // Return the minimum component alignment after validating children, including mixed-width aggregates.
        std::expected<uint32_t, std::string>
        validateLayout(const ShaderParameter& node, uint32_t depth, uint32_t& nodes)
        {
            if (depth > 64 || ++nodes > 100000 || node.size > kMaximumBytes ||
                std::ranges::count(node.offsets, ShaderOffsetKind::eUniform, &ShaderParameterOffset::kind) > 1)
            {
                return std::unexpected("Invalid or excessively large uniform layout");
            }
            if (node.kind == ShaderParameterKind::eResource || node.kind == ShaderParameterKind::eSampler ||
                node.kind == ShaderParameterKind::eConstantBuffer || node.kind == ShaderParameterKind::eParameterBlock)
            {
                if (node.size != 0)
                {
                    return std::unexpected("Nested resource/container occupies uniform bytes: " + node.name);
                }
                return 1;
            }
            if (node.kind == ShaderParameterKind::eStruct)
            {
                std::vector<std::string_view>              names;
                std::vector<std::pair<uint64_t, uint64_t>> ranges;
                uint32_t                                   alignment = 1;
                for (const auto& field : node.fields)
                {
                    if (field.name.empty())
                    {
                        return std::unexpected("Uniform struct contains an unnamed field");
                    }
                    names.push_back(field.name);
                    auto valid = validateLayout(field, depth + 1, nodes);
                    if (!valid)
                    {
                        return valid;
                    }
                    const auto offset = field.offset(ShaderOffsetKind::eUniform);
                    if (field.size)
                    {
                        if (offset % *valid != 0 || offset > node.size || field.size > node.size - offset)
                        {
                            return std::unexpected("Uniform field lies outside its struct: " + field.name);
                        }
                        ranges.emplace_back(offset, offset + field.size);
                        alignment = std::max(alignment, *valid);
                    }
                }
                std::ranges::sort(names);
                if (std::ranges::adjacent_find(names) != names.end())
                {
                    return std::unexpected("Uniform struct contains duplicate field names");
                }
                std::ranges::sort(ranges);
                for (size_t i = 1; i < ranges.size(); ++i)
                {
                    if (ranges[i].first < ranges[i - 1].second)
                    {
                        return std::unexpected("Reflected uniform fields overlap");
                    }
                }
                return alignment;
            }
            if (node.kind == ShaderParameterKind::eArray)
            {
                // Descriptor arrays have no uniform storage. Their binding validation is separate.
                if (!node.size)
                {
                    if (node.fields.size() != 1 || node.fields.front().size != 0)
                    {
                        return std::unexpected("Zero-size uniform array contains data: " + node.name);
                    }
                    return validateLayout(node.fields.front(), depth + 1, nodes);
                }
                if (node.fields.size() != 1 || !node.count || !node.stride)
                {
                    return std::unexpected("Invalid uniform array layout: " + node.name);
                }
                const auto& element = node.fields.front();
                auto        valid   = validateLayout(element, depth + 1, nodes);
                if (!valid)
                {
                    return valid;
                }
                const auto  offset  = element.offset(ShaderOffsetKind::eUniform);
                if (node.stride % *valid != 0 || offset % *valid != 0 || offset > node.stride ||
                    element.size > node.stride - offset || offset > node.size || element.size > node.size - offset ||
                    node.count - 1 > (node.size - offset - element.size) / node.stride)
                {
                    return std::unexpected("Uniform array stride/count exceeds storage: " + node.name);
                }
                return *valid;
            }
            if (!node.fields.empty())
            {
                return std::unexpected("Uniform numeric type contains unexpected fields: " + node.name);
            }
            const auto bytes = scalarSize(node.scalar);
            if (!bytes)
            {
                return std::unexpected("Uniform block has an unsupported scalar type: " + node.name);
            }
            if (node.kind == ShaderParameterKind::eScalar && node.size == bytes)
            {
                return bytes;
            }
            if (node.kind == ShaderParameterKind::eVector && node.columns >= 1 && node.columns <= 4 &&
                node.size == uint64_t(node.columns) * bytes)
            {
                return bytes;
            }
            if (node.kind == ShaderParameterKind::eMatrix && node.rows >= 1 && node.rows <= 4 && node.columns >= 1 &&
                node.columns <= 4)
            {
                const auto major = node.columnMajor ? node.columns : node.rows;
                const auto minor = node.columnMajor ? node.rows : node.columns;
                if (node.matrixStride % bytes == 0 && node.matrixStride >= uint64_t(minor) * bytes &&
                    node.matrixStride <= kMaximumBytes &&
                    uint64_t(major - 1) * node.matrixStride + uint64_t(minor) * bytes <= node.size)
                {
                    return bytes;
                }
            }
            return std::unexpected("Uniform type size/shape does not match reflection: " + node.name);
        }

        struct Location
        {
            const ShaderParameter* node;
            uint64_t               offset;
        };

        std::expected<Location, std::string> resolve(const ShaderParameter& root, std::string_view path)
        {
            if (path.size() > 4096)
            {
                return std::unexpected("Uniform path exceeds 4096 bytes");
            }
            Location result {&root, 0};
            size_t   cursor = 0;
            uint32_t depth  = 0;
            while (cursor < path.size())
            {
                if (++depth > 64)
                {
                    return std::unexpected("Uniform path exceeds the layout depth limit");
                }
                uint64_t offset = 0;
                if (path[cursor] == '[')
                {
                    const auto end = path.find(']', cursor + 1);
                    if (result.node->kind != ShaderParameterKind::eArray || result.node->fields.size() != 1 ||
                        end == std::string_view::npos)
                    {
                        return std::unexpected("Invalid uniform array selection: " + std::string(path));
                    }
                    uint64_t   index  = 0;
                    const auto parsed = std::from_chars(path.data() + cursor + 1, path.data() + end, index);
                    if (parsed.ec != std::errc {} || parsed.ptr != path.data() + end || index >= result.node->count ||
                        !result.node->stride || index > kMaximumBytes / result.node->stride)
                    {
                        return std::unexpected("Uniform array index is out of range: " + std::string(path));
                    }
                    offset      = index * result.node->stride;
                    result.node = &result.node->fields.front();
                    cursor      = end + 1;
                }
                else
                {
                    const auto end = path.find_first_of(".[", cursor);
                    const auto name =
                        path.substr(cursor, end == std::string_view::npos ? path.size() - cursor : end - cursor);
                    if (result.node->kind != ShaderParameterKind::eStruct || name.empty())
                    {
                        return std::unexpected("Invalid uniform struct selection: " + std::string(path));
                    }
                    result.node = result.node->field(name);
                    if (!result.node)
                    {
                        return std::unexpected("Unknown uniform field: " + std::string(path));
                    }
                    cursor += name.size();
                }
                const auto fieldOffset = result.node->offset(ShaderOffsetKind::eUniform);
                if (fieldOffset > kMaximumBytes || offset > kMaximumBytes - fieldOffset ||
                    result.offset > kMaximumBytes - offset - fieldOffset)
                {
                    return std::unexpected("Uniform path offset exceeds storage");
                }
                result.offset += offset + fieldOffset;
                if (cursor < path.size() && path[cursor] == '.')
                {
                    if (++cursor == path.size() || path[cursor] == '[')
                    {
                        return std::unexpected("Invalid uniform field separator: " + std::string(path));
                    }
                }
                else if (cursor < path.size() && path[cursor] != '[')
                {
                    return std::unexpected("Invalid uniform path separator: " + std::string(path));
                }
            }
            return result;
        }

        template<typename T>
        constexpr ShaderScalarType scalarType()
        {
            if constexpr (std::is_same_v<T, float>)
            {
                return ShaderScalarType::eFloat;
            }
            else if constexpr (std::is_same_v<T, int32_t>)
            {
                return ShaderScalarType::eInteger;
            }
            else if constexpr (std::is_same_v<T, uint32_t>)
            {
                return ShaderScalarType::eUnsigned;
            }
            else if constexpr (std::is_same_v<T, ShaderFloat16>)
            {
                return ShaderScalarType::eFloat16;
            }
            else
            {
                return ShaderScalarType::eBoolean;
            }
        }

        struct Component
        {
            uint64_t offset;
            uint32_t                 size;
            std::array<std::byte, 4> value {};
        };

        uint64_t componentOffset(const ShaderParameter& node, uint32_t row, uint32_t column)
        {
            if (node.kind != ShaderParameterKind::eMatrix)
            {
                return uint64_t(column) * scalarSize(node.scalar);
            }
            const auto major = node.columnMajor ? column : row;
            const auto minor = node.columnMajor ? row : column;
            return uint64_t(major) * node.matrixStride + uint64_t(minor) * scalarSize(node.scalar);
        }

        struct Transfer
        {
            const ShaderParameter* source;
            const ShaderParameter* target;
            uint64_t               sourceOffset = 0;
            uint64_t               targetOffset = 0;
            bool                   copy         = false;
            std::vector<Transfer>  children;
        };

        std::string childPath(std::string_view parent, std::string_view name)
        {
            return parent.empty() ? std::string(name) : std::string(parent) + "." + std::string(name);
        }

        Transfer planTransfer(const ShaderParameter& source,
                              const ShaderParameter& target,
                              std::string_view       path,
                              std::string&           messages)
        {
            Transfer   transfer {&source, &target};
            const auto label = path.empty() ? std::string("<root>") : std::string(path);
            if (!source.size || !target.size)
            {
                if (source.size != target.size)
                {
                    messages += source.size ? "Removed uniform field: " : "Initialized new uniform field: ";
                    messages += label + "\n";
                }
                return transfer;
            }
            if (source.kind != target.kind)
            {
                messages += "Reset incompatible uniform field: " + label + "\n";
                return transfer;
            }
            if (target.kind == ShaderParameterKind::eStruct)
            {
                std::map<std::string_view, const ShaderParameter*, std::less<>> fields;
                for (const auto& field : source.fields)
                {
                    if (field.size)
                    {
                        fields.emplace(field.name, &field);
                    }
                }
                for (const auto& field : target.fields)
                {
                    if (!field.size)
                    {
                        continue;
                    }
                    const auto name  = childPath(path, field.name);
                    const auto found = fields.find(field.name);
                    if (found == fields.end())
                    {
                        messages += "Initialized new uniform field: " + name + "\n";
                        continue;
                    }
                    auto child         = planTransfer(*found->second, field, name, messages);
                    child.sourceOffset = found->second->offset(ShaderOffsetKind::eUniform);
                    child.targetOffset = field.offset(ShaderOffsetKind::eUniform);
                    if (child.copy || !child.children.empty())
                    {
                        transfer.children.push_back(std::move(child));
                    }
                    fields.erase(found);
                }
                for (const auto& field : fields)
                {
                    messages += "Removed uniform field: " + childPath(path, field.first) + "\n";
                }
            }
            else if (target.kind == ShaderParameterKind::eArray && source.size && target.size)
            {
                if (source.count != target.count)
                {
                    messages += "Resized uniform array: " + label + " (" + std::to_string(source.count) + " -> " +
                                std::to_string(target.count) + ")\n";
                }
                auto child         = planTransfer(source.fields.front(), target.fields.front(), label + "[]", messages);
                child.sourceOffset = source.fields.front().offset(ShaderOffsetKind::eUniform);
                child.targetOffset = target.fields.front().offset(ShaderOffsetKind::eUniform);
                if (child.copy || !child.children.empty())
                {
                    transfer.children.push_back(std::move(child));
                }
            }
            else
            {
                const auto sourceRows    = source.kind == ShaderParameterKind::eMatrix ? source.rows : 1;
                const auto targetRows    = target.kind == ShaderParameterKind::eMatrix ? target.rows : 1;
                const auto sourceColumns = source.kind == ShaderParameterKind::eScalar ? 1 : source.columns;
                const auto targetColumns = target.kind == ShaderParameterKind::eScalar ? 1 : target.columns;
                transfer.copy =
                    source.scalar == target.scalar && sourceRows == targetRows && sourceColumns == targetColumns;
                if (!transfer.copy)
                {
                    messages += "Reset incompatible uniform field: " + label + "\n";
                }
            }
            return transfer;
        }

        void applyTransfer(const Transfer&            transfer,
                           std::span<const std::byte> source,
                           std::span<std::byte>       target,
                           uint64_t                   sourceOffset = 0,
                           uint64_t                   targetOffset = 0)
        {
            sourceOffset += transfer.sourceOffset;
            targetOffset += transfer.targetOffset;
            const auto& from = *transfer.source;
            const auto& to   = *transfer.target;
            if (transfer.copy)
            {
                const auto rows    = to.kind == ShaderParameterKind::eMatrix ? to.rows : 1;
                const auto columns = to.kind == ShaderParameterKind::eScalar ? 1 : to.columns;
                for (uint32_t row = 0; row < rows; ++row)
                {
                    for (uint32_t column = 0; column < columns; ++column)
                    {
                        std::memcpy(target.data() + targetOffset + componentOffset(to, row, column),
                                    source.data() + sourceOffset + componentOffset(from, row, column),
                                    scalarSize(to.scalar));
                    }
                }
            }
            else if (to.kind == ShaderParameterKind::eArray && !transfer.children.empty())
            {
                for (uint64_t i = 0; i < std::min(from.count, to.count); ++i)
                {
                    applyTransfer(transfer.children.front(),
                                  source,
                                  target,
                                  sourceOffset + i * from.stride,
                                  targetOffset + i * to.stride);
                }
            }
            else
            {
                for (const auto& child : transfer.children)
                {
                    applyTransfer(child, source, target, sourceOffset, targetOffset);
                }
            }
        }
    } // namespace

    ShaderUniformBlock::ShaderUniformBlock(ShaderParameter layout, std::vector<std::byte> data) :
        m_Layout(std::move(layout)),
        m_Data(std::move(data))
    {
    }

    std::expected<ShaderUniformBlock, std::string> ShaderUniformBlock::create(const ShaderParameter& layout)
    {
        const auto* element = &layout;
        if (layout.kind == ShaderParameterKind::eConstantBuffer || layout.kind == ShaderParameterKind::eParameterBlock)
        {
            element = layout.field("$element");
            if (!element)
            {
                return std::unexpected("Uniform container has no reflected element");
            }
        }
        uint32_t nodes = 0;
        if (element->kind == ShaderParameterKind::eResource || element->kind == ShaderParameterKind::eSampler ||
            element->kind == ShaderParameterKind::eConstantBuffer ||
            element->kind == ShaderParameterKind::eParameterBlock)
        {
            return std::unexpected("Uniform block requires a data type");
        }
        if (auto valid = validateLayout(*element, 0, nodes); !valid)
        {
            return std::unexpected(valid.error());
        }
        return ShaderUniformBlock(*element, std::vector<std::byte>(size_t(element->size)));
    }

    std::expected<ShaderUniformBlock, std::string> ShaderUniformBlock::create(const ShaderParameter&     layout,
                                                                              std::span<const std::byte> bytes)
    {
        auto block = create(layout);
        if (!block)
        {
            return std::unexpected(block.error());
        }
        if (bytes.size() != block->m_Data.size())
        {
            return std::unexpected("Initial uniform bytes do not match the reflected size");
        }
        std::ranges::copy(bytes, block->m_Data.begin());
        return block;
    }

    std::expected<bool, std::string> ShaderUniformBlock::setExpected(std::span<const ShaderUniformWrite> writes)
    {
        if (writes.size() > 100000)
        {
            return std::unexpected("Uniform edit exceeds the field limit");
        }
        std::vector<Component> staged;
        for (const auto& write : writes)
        {
            auto location = resolve(m_Layout, write.path);
            if (!location)
            {
                return std::unexpected(location.error());
            }
            const auto& field   = *location->node;
            const auto  rows    = field.kind == ShaderParameterKind::eMatrix ? field.rows : 1;
            const auto  columns = field.kind == ShaderParameterKind::eScalar ? 1 : field.columns;
            if ((field.kind != ShaderParameterKind::eScalar && field.kind != ShaderParameterKind::eVector &&
                 field.kind != ShaderParameterKind::eMatrix) ||
                write.kind != field.kind || write.rows != rows || write.columns != columns)
            {
                return std::unexpected("Uniform write shape does not match reflection: " + std::string(write.path));
            }
            auto appended = std::visit(
                [&](const auto values) -> Result
                {
                    using T = std::remove_const_t<typename decltype(values)::element_type>;
                    if (field.scalar != scalarType<T>() || values.size() != uint64_t(rows) * columns)
                    {
                        return std::unexpected("Uniform write type/count does not match reflection: " +
                                               std::string(write.path));
                    }
                    for (uint32_t row = 0; row < rows; ++row)
                    {
                        for (uint32_t column = 0; column < columns; ++column)
                        {
                            const auto offset = location->offset + componentOffset(field, row, column);
                            const auto size   = scalarSize(field.scalar);
                            if (offset > m_Data.size() || size > m_Data.size() - offset ||
                                staged.size() >= m_Data.size() / 2)
                            {
                                return std::unexpected("Uniform write exceeds block storage");
                            }
                            Component  component {offset, size};
                            const auto value = values[size_t(row) * columns + column];
                            if constexpr (std::is_same_v<T, bool>)
                            {
                                const uint32_t bits = value ? 1 : 0;
                                std::memcpy(component.value.data(), &bits, sizeof(bits));
                            }
                            else if constexpr (std::is_same_v<T, ShaderFloat16>)
                            {
                                std::memcpy(component.value.data(), &value.bits, sizeof(value.bits));
                            }
                            else
                            {
                                std::memcpy(component.value.data(), &value, sizeof(value));
                            }
                            staged.push_back(component);
                        }
                    }
                    return {};
                },
                write.values);
            if (!appended)
            {
                return std::unexpected(appended.error());
            }
        }
        std::ranges::sort(staged, {}, &Component::offset);
        bool changed = false;
        for (size_t i = 0; i < staged.size(); ++i)
        {
            const auto& word = staged[i];
            if (i && word.offset < staged[i - 1].offset + staged[i - 1].size)
            {
                return std::unexpected("Uniform writes overlap");
            }
            changed |= std::memcmp(m_Data.data() + word.offset, word.value.data(), word.size) != 0;
        }
        if (!changed)
        {
            return false;
        }
        if (m_Revision == std::numeric_limits<uint64_t>::max())
        {
            return std::unexpected("Uniform block revision is exhausted");
        }
        for (const auto& word : staged)
        {
            std::memcpy(m_Data.data() + word.offset, word.value.data(), word.size);
        }
        ++m_Revision;
        return true;
    }

    std::expected<ShaderUniformBlock, std::string> ShaderUniformBlock::reconcileExpected(const ShaderParameter& layout,
                                                                                         std::string& diagnostics) const
    {
        if (m_Data.size() != m_Layout.size)
        {
            return std::unexpected("Uniform source storage does not match its layout");
        }
        auto candidate = create(layout);
        if (!candidate)
        {
            return std::unexpected(candidate.error());
        }
        std::string messages;
        const auto  transfer = planTransfer(m_Layout, candidate->m_Layout, "", messages);
        applyTransfer(transfer, m_Data, candidate->m_Data);
        candidate->m_Revision = m_Revision;
        if (m_Layout != candidate->m_Layout || m_Data != candidate->m_Data)
        {
            if (m_Revision == std::numeric_limits<uint64_t>::max())
            {
                return std::unexpected("Uniform block revision is exhausted");
            }
            ++candidate->m_Revision;
        }
        diagnostics += messages;
        return candidate;
    }

    const ShaderParameter& ShaderUniformBlock::layout() const
    {
        return m_Layout;
    }

    std::span<const std::byte> ShaderUniformBlock::data() const
    {
        return m_Data;
    }

    uint64_t ShaderUniformBlock::revision() const
    {
        return m_Revision;
    }
} // namespace vultra
