[[vk::binding(0, 0)]] RWStructuredBuffer<uint4> output;
[[vk::binding(1, 0)]] StructuredBuffer<uint> inputs[4];
struct Parameters
{
    uint selected;
    uint destination;
    uint addend;
    uint unused;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    output[parameters.destination] = uint4(inputs[parameters.selected][0],
                                          inputs[parameters.selected][3],
                                          parameters.selected,
                                          parameters.addend);
}
