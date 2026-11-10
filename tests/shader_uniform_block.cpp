#include <vultra/drivers/rhi/shader_compiler.hpp>
#include <vultra/drivers/rhi/shader_uniform_block.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;

    ShaderParameter numeric(std::string name, ShaderScalarType scalar, uint64_t offset)
    {
        ShaderParameter field;
        field.name   = std::move(name);
        field.kind   = ShaderParameterKind::eScalar;
        field.scalar = scalar;
        field.size   = 4;
        field.offsets.push_back({ShaderOffsetKind::eUniform, offset, 0});
        return field;
    }

    ShaderParameter layout()
    {
        auto tint             = numeric("tint", ShaderScalarType::eFloat, 0);
        tint.kind             = ShaderParameterKind::eVector;
        tint.columns          = 3;
        tint.size             = 12;
        auto column           = numeric("transform", ShaderScalarType::eFloat, 16);
        column.kind           = ShaderParameterKind::eMatrix;
        column.rows           = 3;
        column.columns        = 2;
        column.size           = 32;
        column.matrixStride   = 16;
        auto row              = column;
        row.name              = "rows";
        row.rows              = 2;
        row.columns           = 3;
        row.columnMajor       = false;
        row.offsets[0].offset = 48;
        ShaderParameter item;
        item.name   = "$element";
        item.kind   = ShaderParameterKind::eStruct;
        item.size   = 80;
        item.fields = {tint, numeric("weight", ShaderScalarType::eFloat, 12), column, row};
        ShaderParameter items;
        items.name   = "items";
        items.kind   = ShaderParameterKind::eArray;
        items.size   = 160;
        items.count  = 2;
        items.stride = 80;
        items.offsets.push_back({ShaderOffsetKind::eUniform, 16, 0});
        items.fields.push_back(item);
        ShaderParameter weights;
        weights.name   = "weights";
        weights.kind   = ShaderParameterKind::eArray;
        weights.size   = 32;
        weights.count  = 2;
        weights.stride = 16;
        weights.offsets.push_back({ShaderOffsetKind::eUniform, 176, 0});
        auto inner   = weights;
        inner.name   = "$element";
        inner.size   = 12;
        inner.count  = 3;
        inner.stride = 4;
        inner.offsets.clear();
        inner.fields   = {numeric("$element", ShaderScalarType::eFloat, 0)};
        weights.fields = {inner};
        ShaderParameter root;
        root.kind   = ShaderParameterKind::eStruct;
        root.size   = 208;
        root.fields = {numeric("enabled", ShaderScalarType::eBoolean, 0),
                       numeric("count", ShaderScalarType::eInteger, 4),
                       numeric("index", ShaderScalarType::eUnsigned, 8),
                       numeric("gain", ShaderScalarType::eFloat, 12),
                       items,
                       weights};
        return root;
    }

    Result checkedValues()
    {
        auto source = layout();
        auto block  = ShaderUniformBlock::create(source);
        if (!block)
        {
            return std::unexpected(block.error());
        }
        const std::array flags {true};
        const std::array count {int32_t(-3)};
        const std::array index {uint32_t(42)};
        const std::array gain {2.f};
        const std::array tint {1.f, 2.f, 3.f};
        const std::array weight {0.5f};
        const std::array column {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
        const std::array row {7.f, 8.f, 9.f, 10.f, 11.f, 12.f};
        const std::array last {9.f};
        const std::array edits {
            ShaderUniformWrite {"enabled", std::span(flags)},
            ShaderUniformWrite {"count", std::span(count)},
            ShaderUniformWrite {"index", std::span(index)},
            ShaderUniformWrite {"gain", std::span(gain)},
            ShaderUniformWrite {"items[1].tint", std::span(tint), ShaderParameterKind::eVector, 1, 3},
            ShaderUniformWrite {"items[1].weight", std::span(weight)},
            ShaderUniformWrite {"items[1].transform", std::span(column), ShaderParameterKind::eMatrix, 3, 2},
            ShaderUniformWrite {"items[1].rows", std::span(row), ShaderParameterKind::eMatrix, 2, 3},
            ShaderUniformWrite {"weights[1][2]", std::span(last)}};
        auto accepted = block->setExpected(edits);
        if (!accepted || !*accepted || block->revision() != 1)
        {
            return std::unexpected(accepted ? "Uniform edit did not advance once" : accepted.error());
        }
        std::vector<std::byte> expected(208);
        const auto             put = [&](size_t offset, const auto value)
        {
            std::memcpy(expected.data() + offset, &value, 4);
        };
        put(0, uint32_t(1));
        put(4, int32_t(-3));
        put(8, uint32_t(42));
        put(12, 2.f);
        put(96, 1.f);
        put(100, 2.f);
        put(104, 3.f);
        put(108, 0.5f);
        put(112, 1.f);
        put(116, 3.f);
        put(120, 5.f);
        put(128, 2.f);
        put(132, 4.f);
        put(136, 6.f);
        put(144, 7.f);
        put(148, 8.f);
        put(152, 9.f);
        put(160, 10.f);
        put(164, 11.f);
        put(168, 12.f);
        put(200, 9.f);
        if (!std::ranges::equal(block->data(), expected))
        {
            return std::unexpected(
                "Nested uniform fields, bool width, matrix order or padding differ from the byte oracle");
        }
        accepted   = block->setExpected(edits);
        auto empty = block->setExpected({});
        if (!accepted || *accepted || !empty || *empty || block->revision() != 1)
        {
            return std::unexpected("Identity/empty uniform edit changed its revision");
        }
        const std::array<float, 1> changed {3.f};
        const std::array           invalidPaths {".gain",
                                       "gain.",
                                       "items[-1].weight",
                                       "items[2].weight",
                                       "items[18446744073709551616].weight",
                                       "items[0]weight",
                                       "weights[0][3]",
                                       "gain[0]",
                                       "missing"};
        for (const auto path : invalidPaths)
        {
            const std::array rejected {ShaderUniformWrite {"gain", std::span(changed)},
                                       ShaderUniformWrite {path, std::span(weight)}};
            if (block->setExpected(rejected) || block->revision() != 1 || !std::ranges::equal(block->data(), expected))
            {
                return std::unexpected("Invalid uniform path partially changed the accepted block");
            }
        }
        const std::array invalidTypes {
            ShaderUniformWrite {"enabled", std::span(index)},
            ShaderUniformWrite {"index", std::span(count)},
            ShaderUniformWrite {"gain", std::span(gain), ShaderParameterKind::eVector},
            ShaderUniformWrite {"items[1].tint", std::span(tint), ShaderParameterKind::eVector, 3, 1},
            ShaderUniformWrite {"items[1].transform", std::span(column), ShaderParameterKind::eMatrix, 2, 3}};
        for (const auto& invalid : invalidTypes)
        {
            const std::array rejected {ShaderUniformWrite {"gain", std::span(changed)}, invalid};
            if (block->setExpected(rejected) || block->revision() != 1 || !std::ranges::equal(block->data(), expected))
            {
                return std::unexpected("Invalid uniform type/shape partially changed the accepted block");
            }
        }
        const std::array overlapping {ShaderUniformWrite {"gain", std::span(changed)}, edits[3]};
        if (block->setExpected(overlapping) || block->revision() != 1 || !std::ranges::equal(block->data(), expected))
        {
            return std::unexpected("Overlapping uniform writes changed accepted bytes");
        }
        source.fields[3].scalar = ShaderScalarType::eInteger;
        const std::array final {ShaderUniformWrite {"gain", std::span(changed)}};
        if (auto owned = block->setExpected(final); !owned || !*owned || block->revision() != 2)
        {
            return std::unexpected("Uniform block borrowed its caller's mutable reflection tree");
        }
        std::puts(
            "UNIFORM_BLOCK_VALUES nested_arrays=1 matrices=2 bool32=1 atomic=1 overlap=1 identity=1 owned_layout=1");
        return {};
    }

    ShaderParameter migratedLayout()
    {
        auto root                   = layout();
        root.size                   = 432;
        auto weights                = root.fields[5];
        weights.offsets[0].offset   = 0;
        weights.count               = 3;
        weights.size                = 48;
        weights.fields[0].count     = 4;
        weights.fields[0].size      = 16;
        auto items                  = root.fields[4];
        items.offsets[0].offset     = 80;
        items.count                 = 3;
        items.stride                = 112;
        items.size                  = 336;
        auto& item                  = items.fields[0];
        item.size                   = 112;
        auto rows                   = item.fields[3];
        rows.columnMajor            = true;
        rows.size                   = 48;
        rows.offsets[0].offset      = 0;
        auto weight                 = item.fields[1];
        weight.offsets[0].offset    = 48;
        auto tint                   = item.fields[0];
        tint.offsets[0].offset      = 64;
        auto transform              = item.fields[2];
        transform.columnMajor       = false;
        transform.matrixStride      = 8;
        transform.size              = 24;
        transform.offsets[0].offset = 80;
        item.fields                 = {rows, weight, tint, transform};
        root.fields                 = {weights,
                                       numeric("gain", ShaderScalarType::eFloat, 48),
                                       numeric("enabled", ShaderScalarType::eBoolean, 52),
                                       numeric("count", ShaderScalarType::eInteger, 56),
                                       numeric("index", ShaderScalarType::eFloat, 60),
                                       numeric("fresh", ShaderScalarType::eUnsigned, 64),
                                       items};
        return root;
    }

    Result reconciledValues()
    {
        auto source = layout();
        source.size = 212;
        source.fields.push_back(numeric("obsolete", ShaderScalarType::eUnsigned, 208));
        auto block = ShaderUniformBlock::create(source);
        if (!block)
        {
            return std::unexpected(block.error());
        }
        const std::array enabled {true};
        const std::array count {int32_t(-3)};
        const std::array index {uint32_t(17)};
        const std::array gain {.75f};
        const std::array tint {1.f, 2.f, 3.f};
        const std::array transform {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
        const std::array rows {7.f, 8.f, 9.f, 10.f, 11.f, 12.f};
        const std::array weight {42.f};
        const std::array obsolete {uint32_t(99)};
        const std::array edits {
            ShaderUniformWrite {"enabled", std::span(enabled)},
            ShaderUniformWrite {"count", std::span(count)},
            ShaderUniformWrite {"index", std::span(index)},
            ShaderUniformWrite {"gain", std::span(gain)},
            ShaderUniformWrite {"items[0].tint", std::span(tint), ShaderParameterKind::eVector, 1, 3},
            ShaderUniformWrite {"items[0].transform", std::span(transform), ShaderParameterKind::eMatrix, 3, 2},
            ShaderUniformWrite {"items[1].rows", std::span(rows), ShaderParameterKind::eMatrix, 2, 3},
            ShaderUniformWrite {"weights[1][2]", std::span(weight)},
            ShaderUniformWrite {"obsolete", std::span(obsolete)}};
        if (auto edited = block->setExpected(edits); !edited)
        {
            return std::unexpected(edited.error());
        }
        const std::vector<std::byte> original(block->data().begin(), block->data().end());
        auto                         bytes    = original;
        auto                         imported = ShaderUniformBlock::create(source, bytes);
        std::ranges::fill(bytes, std::byte {0xff});
        if (!imported || imported->revision() != 0 || !std::ranges::equal(imported->data(), original) ||
            ShaderUniformBlock::create(source, std::span(original).first(1)))
        {
            return std::unexpected("Initial uniform payload was not size-checked and independently owned");
        }
        auto                         target      = migratedLayout();
        std::string                  diagnostics = "prefix\n";
        auto                         candidate   = block->reconcileExpected(target, diagnostics);
        if (!candidate)
        {
            return std::unexpected(candidate.error());
        }
        std::vector<std::byte> expected(432);
        const auto             put = [&](size_t offset, auto value)
        {
            std::memcpy(expected.data() + offset, &value, 4);
        };
        put(24, 42.f);
        put(48, .75f);
        put(52, uint32_t(1));
        put(56, int32_t(-3));
        for (size_t i = 0; i < tint.size(); ++i)
        {
            put(144 + i * 4, tint[i]);
        }
        for (size_t i = 0; i < transform.size(); ++i)
        {
            put(160 + i * 4, transform[i]);
        }
        const std::array<size_t, 6> rowOffsets {192, 208, 224, 196, 212, 228};
        for (size_t i = 0; i < rows.size(); ++i)
        {
            put(rowOffsets[i], rows[i]);
        }
        if (!std::ranges::equal(candidate->data(), expected) || candidate->revision() != 2 || block->revision() != 1 ||
            !std::ranges::equal(block->data(), original) || !diagnostics.starts_with("prefix\n") ||
            diagnostics.find("Removed uniform field: obsolete") == std::string::npos ||
            diagnostics.find("Reset incompatible uniform field: index") == std::string::npos ||
            diagnostics.find("Initialized new uniform field: fresh") == std::string::npos ||
            diagnostics.find("Resized uniform array: weights[]") == std::string::npos)
        {
            return std::unexpected(
                "Uniform reconciliation lost typed values, storage layout, diagnostics or old state");
        }
        std::string unchanged = "unchanged";
        auto        identity  = candidate->reconcileExpected(target, unchanged);
        if (!identity || identity->revision() != 2 || unchanged != "unchanged" ||
            !std::ranges::equal(identity->data(), expected))
        {
            return std::unexpected("Identity uniform reconciliation changed revision, bytes or diagnostics");
        }
        auto changedShape                                  = target;
        changedShape.fields[6].fields[0].fields[2].columns = 2;
        changedShape.fields[6].fields[0].fields[2].size    = 8;
        std::string shapeMessages;
        auto        shape         = block->reconcileExpected(changedShape, shapeMessages);
        auto        shapeExpected = expected;
        for (size_t item = 0; item < 3; ++item)
        {
            std::fill_n(shapeExpected.begin() + 80 + item * 112 + 64, 12, std::byte {0});
        }
        if (!shape || !std::ranges::equal(shape->data(), shapeExpected) ||
            shapeMessages.find("Reset incompatible uniform field: items[].tint") == std::string::npos)
        {
            return std::unexpected("Changed vector shape reinterpreted old components");
        }
        auto  changedRank = target;
        auto& rank        = changedRank.fields[0];
        rank.count        = 12;
        rank.stride       = 4;
        rank.fields       = {numeric("$element", ShaderScalarType::eFloat, 0)};
        std::string rankMessages;
        auto        ranked       = block->reconcileExpected(changedRank, rankMessages);
        auto        rankExpected = expected;
        std::fill_n(rankExpected.begin(), 48, std::byte {0});
        if (!ranked || !std::ranges::equal(ranked->data(), rankExpected) ||
            rankMessages.find("Reset incompatible uniform field: weights[]") == std::string::npos)
        {
            return std::unexpected("Changed array rank reinterpreted old elements");
        }
        auto shrunk            = target;
        shrunk.fields[6].count = 1;
        shrunk.fields[6].size  = 112;
        std::string shrinkMessages;
        auto        smaller        = block->reconcileExpected(shrunk, shrinkMessages);
        auto        shrinkExpected = expected;
        std::fill(shrinkExpected.begin() + 192, shrinkExpected.end(), std::byte {0});
        if (!smaller || !std::ranges::equal(smaller->data(), shrinkExpected) ||
            shrinkMessages.find("Resized uniform array: items (2 -> 1)") == std::string::npos)
        {
            return std::unexpected("Shrinking an array failed to preserve its common prefix");
        }
        auto invalid         = target;
        invalid.size         = 8;
        std::string retained = "retained";
        if (block->reconcileExpected(invalid, retained) || retained != "retained" || block->revision() != 1 ||
            !std::ranges::equal(block->data(), original))
        {
            return std::unexpected("Malformed layout reconciliation changed accepted values or diagnostics");
        }
        auto owned = std::move(*block);
        if (block->reconcileExpected(target, retained) || retained != "retained" ||
            !std::ranges::equal(owned.data(), original))
        {
            return std::unexpected("Moved-from uniform source was read as valid storage");
        }
        std::puts("UNIFORM_RECONCILE_CPU bytes=432 offsets=1 array_stride=1 nested_resize=1 matrix_major=2 "
                  "type_shape_rank_reset=1 shrink=1 identity=1 failure_atomic=1 initial_bytes=1");
        return {};
    }

    Result rejectedLayouts()
    {
        for (unsigned test = 0; test < 9; ++test)
        {
            auto invalid = layout();
            switch (test)
            {
                case 0:
                    invalid.size = 16 * 1024 * 1024 + 1;
                    break;
                case 1:
                    invalid.fields[1].name = invalid.fields[0].name;
                    break;
                case 2:
                    invalid.fields[1].offsets[0].offset = 0;
                    break;
                case 3:
                    invalid.fields[4].stride = 4;
                    break;
                case 4:
                    invalid.fields[4].count = UINT64_MAX;
                    break;
                case 5:
                    invalid.fields[4].fields[0].fields[2].matrixStride = 5;
                    break;
                case 6:
                    invalid.fields[3].offsets[0].offset = UINT64_MAX;
                    break;
                case 7:
                    invalid.fields[3].size = 8;
                    break;
                case 8:
                    invalid.fields[4].size = 0;
                    break;
            }
            if (ShaderUniformBlock::create(invalid))
            {
                return std::unexpected("Uniform block accepted malformed reflection case " + std::to_string(test));
            }
        }
        ShaderParameter missing;
        missing.kind = ShaderParameterKind::eParameterBlock;
        if (ShaderUniformBlock::create(missing))
        {
            return std::unexpected("Uniform block accepted a missing container element");
        }
        std::puts("UNIFORM_BLOCK_LAYOUT rejected=10");
        return {};
    }

    Result directSlang()
    {
        auto compiler = ShaderCompiler::create({});
        if (!compiler)
        {
            return std::unexpected(compiler.error());
        }
        constexpr std::string_view source  = R"(
struct Item { float3 tint; float weight; column_major float3x2 transform; row_major float2x3 rows; };
struct Values { bool enabled; int count; uint index; float gain; Item items[2]; float weights[2][3]; };
[[vk::binding(0, 0)]] ConstantBuffer<Values> values;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> result;
[shader("compute")]
[numthreads(1, 1, 1)] void main()
{
    result[0] = float4(values.items[1].tint.x * values.gain, values.weights[1][2],
        values.items[1].transform[2][1] + values.items[1].rows[1][2],
        float(values.count) + float(values.index) + float(values.enabled));
})";
        auto                       program = (*compiler)->compileSourceExpected("uniform-block.slang", source);
        if (!program)
        {
            return std::unexpected(program.error());
        }
        const auto* values = program->parameters.field("values");
        if (!values)
        {
            return std::unexpected("Direct Slang uniform block was not reflected");
        }
        auto block = ShaderUniformBlock::create(*values);
        if (!block)
        {
            return std::unexpected(block.error());
        }
        const std::array<float, 6> matrix {1, 2, 3, 4, 5, 6};
        const std::array<float, 1> sample {9};
        const std::array           writes {
            ShaderUniformWrite {"items[1].transform", std::span(matrix), ShaderParameterKind::eMatrix, 3, 2},
            ShaderUniformWrite {"items[1].rows", std::span(matrix), ShaderParameterKind::eMatrix, 2, 3},
            ShaderUniformWrite {"weights[1][2]", std::span(sample)}};
        auto edited = block->setExpected(writes);
        if (!edited || !*edited)
        {
            return std::unexpected(edited ? "Direct Slang edit did not change" : edited.error());
        }
        const auto identity = ShaderProgram::compilerVersion();
        const auto version  = identity.substr(0, identity.find(';'));
        std::printf("UNIFORM_BLOCK_SLANG bytes=%zu matrix_array=1 compiler=%s\n",
                    block->data().size(),
                    version.c_str());
        return {};
    }
} // namespace

int main()
{
    for (const auto test : {checkedValues, reconciledValues, rejectedLayouts, directSlang})
    {
        if (auto checked = test(); !checked)
        {
            std::fprintf(stderr, "%s\n", checked.error().c_str());
            return 1;
        }
    }
    return 0;
}
