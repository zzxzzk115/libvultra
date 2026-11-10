import type_conformance_types;

struct Parameters
{
    uint4 tags;
    uint4 value;
};

[[vk::binding(5, 2)]] ConstantBuffer<Parameters> parameters;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint4> result;

[shader("compute")]
[numthreads(4, 1, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
    uint4 payload = parameters.value + thread.x;
    IMaterialModel model = createDynamicObject<IMaterialModel, uint4>(parameters.tags[thread.x], payload);
    IMarker marker = createDynamicObject<IMarker, uint4>(3, payload);
    result[thread.x] = model.evaluate() + marker.marker();
}
