#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>

#include <variant>

namespace vultra
{
    // Exact IEEE 754 binary16 payload. Conversion/rounding is explicit at the caller's numeric boundary.
    struct ShaderFloat16
    {
        uint16_t bits;
        bool     operator==(const ShaderFloat16&) const = default;
    };

    // Borrowed only for setExpected(). Components are dense, with matrices supplied in row-major order.
    struct ShaderUniformWrite
    {
        using Values = std::variant<std::span<const float>,
                                    std::span<const int32_t>,
                                    std::span<const uint32_t>,
                                    std::span<const bool>,
                                    std::span<const ShaderFloat16>>;
        std::string_view    path;
        Values              values;
        ShaderParameterKind kind    = ShaderParameterKind::eScalar;
        uint32_t            rows    = 1;
        uint32_t            columns = 1;
    };

    // Owns reflected CPU layout and bytes. Resource bindings and GPU allocations remain with their existing owners.
    class ShaderUniformBlock
    {
    public:
        // Accept a uniform type or the element of a reflected ConstantBuffer/ParameterBlock. Initially zero-filled.
        static std::expected<ShaderUniformBlock, std::string> create(const ShaderParameter& layout);
        // Copies an exact prepacked payload after layout/size validation. Initial revision remains zero.
        static std::expected<ShaderUniformBlock, std::string> create(const ShaderParameter&     layout,
                                                                     std::span<const std::byte> bytes);
        // Resolve struct.field[index] paths and validate the whole batch before changing bytes or revision.
        // Exact scalar types/widths and logical shapes are required. Overlapping writes reject the batch.
        std::expected<bool, std::string> setExpected(std::span<const ShaderUniformWrite> writes);
        // Creates an independent layout candidate. Compatible named values survive; new/incompatible values are zero.
        // Success appends reconciliation diagnostics. Failure preserves both this block and diagnostics.
        std::expected<ShaderUniformBlock, std::string> reconcileExpected(const ShaderParameter& layout,
                                                                         std::string&           diagnostics) const;
        const ShaderParameter&                         layout() const;
        std::span<const std::byte>       data() const;
        uint64_t                         revision() const;

    private:
        ShaderUniformBlock(ShaderParameter layout, std::vector<std::byte> data);
        ShaderParameter        m_Layout;
        std::vector<std::byte> m_Data;
        uint64_t               m_Revision = 0;
    };
} // namespace vultra
