#include "resources/frame_block.slangh"
#include "resources/primitive_transform.slangh"

[shader("vertex")] SurfaceVertex vertexMain(MeshVertex input)
{
    SurfaceVertex result;
    PrimitiveTransform instance = primitiveTransforms[material.instance.x];
    result.worldPosition = mul(instance.model, float4(input.position, 1)).xyz;
    result.position      = mul(frame.lightViewProjection[uint(material.flags.z)], float4(result.worldPosition, 1));
    result.normal        = mul(instance.normal, float4(input.normal, 0)).xyz;
    result.uv            = input.uv;
    result.color         = input.color;
    result.tangent       = float4(mul(instance.model, float4(input.tangent.xyz, 0)).xyz,
                                  input.tangent.w * instance.properties.x);
    return result;
}

    [shader("fragment")] void fragmentMain(SurfaceVertex input)
{
    readBaseColor(input.uv, input.color);
}
