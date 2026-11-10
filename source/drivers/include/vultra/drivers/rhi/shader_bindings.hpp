#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>
#include <vultra/drivers/rhi/shader_uniform_block.hpp>

#include <memory>

namespace vultra
{
    class Device;
    class Buffer;

    struct ShaderResourceViews
    {
        std::string                       name;
        VriDescriptorType                 type = VriDescriptorType_Texture;
        std::vector<const VriDescriptor*> views;
    };

    // Borrowed only during validation/update. Typed blocks also check their complete reflected layout.
    // Raw bytes are explicitly prepacked input; their type/layout compatibility remains the caller's responsibility.
    struct ShaderUniformUpload
    {
        ShaderUniformUpload(std::string_view name, std::span<const std::byte> bytes);
        ShaderUniformUpload(std::string_view name, const ShaderUniformBlock& block);
        std::string_view           name;
        std::span<const std::byte> bytes;

    private:
        friend class ShaderBindings;
        const ShaderParameter* m_Layout = nullptr;
    };

    // Owns the reflected VRI layout, descriptor sets and selected uniform buffers. Other native views stay borrowed.
    class ShaderBindings
    {
    public:
        // A canonical uniform binding selects its complete array. Alternatively select every element as name[i].
        // Array uploads and uniform queries use name[i], including a reflected array of length one.
        // At most 65536 owned elements across all bindings; native device limits still apply.
        static std::expected<std::unique_ptr<ShaderBindings>, std::string>
        create(Device&                           device,
               const ShaderProgram&              program,
               std::span<const ShaderEntry>      entries,
               std::span<const std::string_view> uniformBlocks = {});
        ~ShaderBindings();
        ShaderBindings(const ShaderBindings&)            = delete;
        ShaderBindings& operator=(const ShaderBindings&) = delete;

        // Owns independent descriptor sets and uniform storage, borrowing this owner's exact native layout.
        // Starts unprepared; accepted values/resources are not copied. This owner and its borrowed layout chain
        // must outlive the instance and every pipeline using that layout. No layout/pipeline is recreated.
        std::expected<std::unique_ptr<ShaderBindings>, std::string> createInstanceExpected() const;

        VriPipelineLayout*                     layout() const;
        std::span<const ShaderResourceBinding> bindings() const;
        // Borrow for explicit read/queue-state tracking. Content writes must use updateExpected().
        std::expected<Buffer*, std::string>    uniformBufferExpected(std::string_view name);
        // Copies accepted CPU upload bytes, not GPU readback. Unknown/uninitialized owned blocks reject.
        std::expected<ShaderUniformBlock, std::string> uniformBlockExpected(std::string_view name) const;
        // Prepares an independent binding/upload candidate using accepted values and explicit new resource views.
        // Success appends diagnostics. Failure preserves the source and diagnostics; pipeline publication stays
        // explicit.
        std::expected<std::unique_ptr<ShaderBindings>, std::string>
                                               reconcileExpected(const ShaderProgram&                 program,
                                                                 std::span<const ShaderEntry>         entries,
                                                                 std::span<const std::string_view>    uniformBlocks,
                                                                 std::span<const ShaderResourceViews> resources,
                                                                 std::string&                         diagnostics) const;
        uint32_t                               pushConstantSize() const;
        // Rejects changes to named parameter types, offsets, shapes or stage visibility without GPU mutation.
        std::expected<void, std::string> validateLayoutExpected(const ShaderProgram&         program,
                                                                std::span<const ShaderEntry> entries) const;
        // Full borrowed-resource batches. Owned uniforms need their first upload and may then reuse accepted bytes.
        std::expected<void, std::string> validateExpected(std::span<const ShaderResourceViews> resources,
                                                          std::span<const ShaderUniformUpload> uniforms = {}) const;
        // Complete prior GPU uses first. Rejection retains every accepted descriptor and readiness state.
        // Maps every changed owned buffer before writing any. Failed maps preserve data and descriptors.
        std::expected<void, std::string> updateExpected(std::span<const ShaderResourceViews> resources,
                                                        std::span<const ShaderUniformUpload> uniforms = {});
        // Pipeline must have been created against layout(). Creates no GPU objects; push data stays caller-owned.
        std::expected<void, std::string> bindExpected(VriCommandBuffer* commands, VriPipeline* pipeline) const;

    private:
        struct State;
        explicit ShaderBindings(std::unique_ptr<State> state);
        std::unique_ptr<State> m_State;
    };
} // namespace vultra
