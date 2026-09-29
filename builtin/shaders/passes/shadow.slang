#include "resources/frame_block.slangh"
#include "resources/gpu_material.slangh"

[shader("vertex")] SurfaceVertex vertexMain(MeshVertex input)
{
    SurfaceVertex result;
    result.position      = mul(frame.lightViewProjection[uint(material.flags.z)], float4(input.position, 1));
    result.worldPosition = input.position;
    result.normal        = input.normal;
    result.uv            = input.uv;
    result.color         = input.color;
    result.tangent       = input.tangent;
    return result;
}

    [shader("fragment")] void fragmentMain(SurfaceVertex input)
{
    readBaseColor(input.uv, input.color);
}
