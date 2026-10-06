#include <vultra/assets/shader_asset.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/shader_material.hpp>

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    constexpr std::string_view kSource = R"shader(Shader "Tests/TargetLayout"
{
    Properties { gain ("Gain", Float) = 1 }
    SubShader { Pass {
        Name "Layout" Compute main
        SLANGPROGRAM
        struct LayoutData
        {
            float3 first;
            float scalar;
            float values[3];
            column_major float3x2 columnMatrix;
            row_major float2x3 rowMatrix;
        };
        [[vk::binding(5, 3)]] ConstantBuffer<LayoutData> extra;
        StructuredBuffer<float> inputs[2];
        RWStructuredBuffer<float4> result;
        [numthreads(1, 1, 1)]
        void main(uint3 index : SV_DispatchThreadID)
        {
            result[0] = float4(extra.first.x + extra.scalar, extra.values[2],
                mul(extra.columnMatrix, float2(2, 3)).y,
                inputs[0][0] + inputs[1][0] + extra.rowMatrix[1][2]) * material.gain;
        }
        ENDSLANG
    } }
})shader";
} // namespace

int main()
try
{
    using namespace vultra;
    const auto root = std::filesystem::path("build/.tmp/shader-layout") / StableId::generate().toString();
    std::filesystem::create_directories(root);
    const auto source = root / "layout.vshader";
    const auto cooked = root / "layout.vshaderc";
    writeFileAtomically(source, std::as_bytes(std::span(kSource.data(), kSource.size())));
    ShaderAsset::compile(source).save(cooked);
    std::filesystem::remove(source);
    const auto  asset   = ShaderAsset::load(cooked);
    const auto& program = asset.subshaders[0].passes[0].program("Default");
    const auto* layout  = program.parameters.field("extra")->field("$element");
    require(layout != nullptr && layout->size > 0, "Extra target layout was lost");
    const auto* array  = layout->field("values");
    const auto* column = layout->field("columnMatrix");
    const auto* row    = layout->field("rowMatrix");
    require(array && array->count == 3 && array->stride > 0 && column && column->columnMajor && row &&
                !row->columnMajor && column->matrixStride && row->matrixStride,
            "Array or explicit matrix target layout was lost");
    std::vector<std::byte> bytes(layout->size);
    auto                   write = [&](uint64_t offset, float value)
    {
        require(offset + sizeof(value) <= bytes.size(), "Reflected field is outside the uniform buffer");
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    };
    write(layout->field("first")->offset(ShaderOffsetKind::eUniform), 1);
    write(layout->field("scalar")->offset(ShaderOffsetKind::eUniform), 2);
    write(array->offset(ShaderOffsetKind::eUniform) + 2 * array->stride, 9);
    auto writeMatrix = [&](const ShaderParameter& matrix, uint32_t y, uint32_t x, float value)
    {
        const auto major = matrix.columnMajor ? x : y;
        const auto minor = matrix.columnMajor ? y : x;
        write(matrix.offset(ShaderOffsetKind::eUniform) + major * matrix.matrixStride + minor * sizeof(float), value);
    };
    writeMatrix(*column, 1, 0, 3);
    writeMatrix(*column, 1, 1, 4);
    writeMatrix(*row, 1, 2, 7);
    Device device;
    Buffer uniforms(device, {bytes.size(), 0, VriBufferUsage_ConstantBuffer, VriMemoryLocation_HostUpload});
    auto*  mapped = device.core.MapBuffer(uniforms.handle, 0, bytes.size());
    require(mapped != nullptr, "Map reflected layout buffer");
    std::memcpy(mapped, bytes.data(), bytes.size());
    device.core.UnmapBuffer(uniforms.handle);
    Buffer                     inputs(device, {32, 4, VriBufferUsage_StorageBuffer, VriMemoryLocation_HostUpload});
    const std::array<float, 2> inputValues {10, 20};
    mapped = device.core.MapBuffer(inputs.handle, 0, 32);
    require(mapped != nullptr, "Map layout inputs");
    std::memcpy(mapped, &inputValues[0], sizeof(float));
    std::memcpy(static_cast<std::byte*>(mapped) + 16, &inputValues[1], sizeof(float));
    device.core.UnmapBuffer(inputs.handle);
    Buffer output(device,
                  {16, 16, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
    Buffer readback(device, {16, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
    std::array<VriDescriptor*, 4> views {};
    const std::array              descriptions {
        VriBufferViewDesc {uniforms.handle, VriDescriptorType_ConstantBuffer, VriFormat_Unknown, 0, bytes.size()},
        VriBufferViewDesc {inputs.handle, VriDescriptorType_StructuredBuffer, VriFormat_Unknown, 0, 4},
        VriBufferViewDesc {inputs.handle, VriDescriptorType_StructuredBuffer, VriFormat_Unknown, 16, 4},
        VriBufferViewDesc {output.handle, VriDescriptorType_StorageBuffer, VriFormat_Unknown, 0, 16}};
    for (uint32_t i = 0; i < views.size(); ++i)
    {
        check(device.core.CreateBufferView(device.handle, &descriptions[i], &views[i]), "Create target layout view");
    }
    {
        MaterialInstance instance;
        ShaderMaterial   material(device, asset, instance);
        const std::array resources {
            ShaderResourceViews {"extra", VriDescriptorType_ConstantBuffer, {views[0]}},
            ShaderResourceViews {"inputs", VriDescriptorType_StructuredBuffer, {views[1], views[2]}},
            ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {views[3]}}};
        material.prepare("Layout", {}, resources);
        Frame frame(device);
        auto* commands = frame.begin();
        inputs.transition(commands, {VriAccess_ShaderResourceRead, VriPipelineStage_ComputeShader});
        uniforms.transition(commands, {VriAccess_ConstantBufferRead, VriPipelineStage_ComputeShader});
        output.transition(commands, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
        material.bind(commands, "Layout");
        const VriDispatchDesc dispatch {1, 1, 1};
        device.core.CmdDispatch(commands, &dispatch);
        output.transition(commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        readback.transition(commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        const VriBufferCopyDesc copy {0, 0, 16};
        device.core.CmdCopyBuffer(commands, readback.handle, output.handle, &copy);
        frame.submitAndWait();
        mapped = device.core.MapBuffer(readback.handle, 0, 16);
        require(mapped != nullptr, "Map target layout readback");
        std::array<float, 4> pixel {};
        std::memcpy(pixel.data(), mapped, 16);
        device.core.UnmapBuffer(readback.handle);
        require(pixel == std::array<float, 4> {3, 9, 18, 37},
                "Target reflection did not match GPU matrix/array bindings");
    }
    for (auto* view : views)
    {
        device.core.DestroyDescriptor(view);
    }
    std::puts("Shader target layout passed: padding, arrays, row/column matrices, resource arrays and sparse "
              "descriptor sets");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
