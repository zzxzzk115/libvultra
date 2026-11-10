struct Item
{
    uint first;
    uint second;
};
#if BUFFER_DIMENSIONS_DIRECT
// Scalar declarations exercise the native direct table, including its usual metadata slot 25.
[[vk::binding(0, 0)]] StructuredBuffer<Item> input0;
[[vk::binding(1, 0)]] StructuredBuffer<Item> input1;
[[vk::binding(2, 0)]] StructuredBuffer<Item> input2;
[[vk::binding(3, 0)]] StructuredBuffer<Item> input3;
[[vk::binding(4, 0)]] StructuredBuffer<Item> input4;
[[vk::binding(5, 0)]] StructuredBuffer<Item> input5;
[[vk::binding(6, 0)]] StructuredBuffer<Item> input6;
[[vk::binding(7, 0)]] StructuredBuffer<Item> input7;
[[vk::binding(8, 0)]] StructuredBuffer<Item> input8;
[[vk::binding(9, 0)]] StructuredBuffer<Item> input9;
[[vk::binding(10, 0)]] StructuredBuffer<Item> input10;
[[vk::binding(11, 0)]] StructuredBuffer<Item> input11;
[[vk::binding(12, 0)]] StructuredBuffer<Item> input12;
[[vk::binding(13, 0)]] StructuredBuffer<Item> input13;
[[vk::binding(14, 0)]] StructuredBuffer<Item> input14;
[[vk::binding(15, 0)]] StructuredBuffer<Item> input15;
[[vk::binding(16, 0)]] StructuredBuffer<Item> input16;
[[vk::binding(17, 0)]] StructuredBuffer<Item> input17;
[[vk::binding(18, 0)]] StructuredBuffer<Item> input18;
[[vk::binding(19, 0)]] StructuredBuffer<Item> input19;
[[vk::binding(20, 0)]] StructuredBuffer<Item> input20;
[[vk::binding(21, 0)]] StructuredBuffer<Item> input21;
[[vk::binding(22, 0)]] StructuredBuffer<Item> input22;
[[vk::binding(23, 0)]] StructuredBuffer<Item> input23;
[[vk::binding(24, 0)]] StructuredBuffer<Item> input24;
[[vk::binding(25, 0)]] StructuredBuffer<Item> input25;
[[vk::binding(26, 0)]] RWByteAddressBuffer output;
#else
[[vk::binding(0, 0)]] StructuredBuffer<Item> inputs[26];
[[vk::binding(1, 0)]] RWByteAddressBuffer output;
#endif
struct Parameters
{
    uint selected;
    uint destination;
    uint addend;
    uint unused;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

struct State
{
    uint value;
    uint offset;
};
uint sampleValue(inout State state, uint mode)
{
    if (mode == 0)
    {
        state.value += state.offset;
        return state.value;
    }
    state.value = (state.value + state.offset) * 3;
    return state.value + mode;
}
uint3 bufferDimensions(StructuredBuffer<Item> input)
{
    uint count;
    uint stride;
    input.GetDimensions(count, stride);
    State state;
    state.value = count * 7;
    state.offset = stride + 1;
    uint value = sampleValue(state, count);
    if (count != 0)
    {
        value += input[count - 1].second;
    }
    return uint3(count, stride, value);
}
uint3 inputDimensions()
{
#if BUFFER_DIMENSIONS_DIRECT
    switch (parameters.selected)
    {
    case 0: return bufferDimensions(input0);
    case 1: return bufferDimensions(input1);
    case 2: return bufferDimensions(input2);
    case 3: return bufferDimensions(input3);
    case 4: return bufferDimensions(input4);
    case 5: return bufferDimensions(input5);
    case 6: return bufferDimensions(input6);
    case 7: return bufferDimensions(input7);
    case 8: return bufferDimensions(input8);
    case 9: return bufferDimensions(input9);
    case 10: return bufferDimensions(input10);
    case 11: return bufferDimensions(input11);
    case 12: return bufferDimensions(input12);
    case 13: return bufferDimensions(input13);
    case 14: return bufferDimensions(input14);
    case 15: return bufferDimensions(input15);
    case 16: return bufferDimensions(input16);
    case 17: return bufferDimensions(input17);
    case 18: return bufferDimensions(input18);
    case 19: return bufferDimensions(input19);
    case 20: return bufferDimensions(input20);
    case 21: return bufferDimensions(input21);
    case 22: return bufferDimensions(input22);
    case 23: return bufferDimensions(input23);
    case 24: return bufferDimensions(input24);
    default: return bufferDimensions(input25);
    }
#else
    return bufferDimensions(inputs[parameters.selected]);
#endif
}
uint4 query()
{
    uint3 dimensions = inputDimensions();
    uint bytes;
    output.GetDimensions(bytes);
    return uint4(dimensions.xy, bytes, dimensions.z + parameters.addend);
}
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    output.Store4(parameters.destination * 16, query());
}
[shader("compute")]
[numthreads(1, 1, 1)]
void alternateMain()
{
    output.Store4(parameters.destination * 16, query() + uint4(0, 0, 0, 1));
}
struct Vertex
{
    float4 position : SV_Position;
    nointerpolation uint3 dimensions : TEXCOORD0;
};
[shader("vertex")]
Vertex vertexMain(uint id : SV_VertexID)
{
    Vertex result;
    result.position = float4(id == 1 ? 3 : -1, id == 2 ? 3 : -1, 0, 1);
    result.dimensions = inputDimensions();
    return result;
}
[shader("vertex")]
Vertex streamVertexMain(float2 position : POSITION)
{
    Vertex result;
    result.position = float4(position, 0, 1);
    result.dimensions = inputDimensions();
    return result;
}
[shader("vertex")]
Vertex noQueryVertexMain(uint id : SV_VertexID)
{
    Vertex result;
    result.position = float4(id == 1 ? 3 : -1, id == 2 ? 3 : -1, 0, 1);
    result.dimensions = uint3(1, 8, 0);
    return result;
}
[shader("fragment")]
uint4 fragmentMain(Vertex input) : SV_Target
{
    uint bytes;
    output.GetDimensions(bytes);
    return uint4(input.dimensions.xy, bytes, input.dimensions.z + parameters.addend);
}
