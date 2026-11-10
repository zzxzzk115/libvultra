// Ported from the PVW companion visibility raster shader.
#include "resources/frame_block.slangh"
#include "resources/primitive_transform.slangh"
#include "resources/gpu_material.slangh"

struct OpaqueVisibilityVertex
{
    float4 position : SV_Position;
    nointerpolation uint instance : TEXCOORD0;
};

struct VisibilityVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint instance : TEXCOORD1;
};

struct VisibilityInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VisibilityDepthVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
OpaqueVisibilityVertex opaqueVertexMain(float3 position : POSITION, uint instance : SV_VulkanInstanceID)
{
    OpaqueVisibilityVertex result;
    // Match the alpha-tested path's camera projection and use the draw's first instance as its transform index.
    precise float3 worldPosition = mul(primitiveTransforms[instance].model, float4(position, 1)).xyz;
    precise float4 clipPosition = mul(frame.viewProjection, float4(worldPosition, 1));
    result.position = clipPosition;
    result.instance = instance;
    return result;
}

[shader("vertex")]
VisibilityVertex vertexMain(VisibilityInput input)
{
    VisibilityVertex result;
    // Match the precise camera projection used by transformSurfaceVertex().
    precise float3 worldPosition = mul(primitiveTransforms[material.instance.x].model, float4(input.position, 1)).xyz;
    precise float4 clipPosition = mul(frame.viewProjection, float4(worldPosition, 1));
    result.position = clipPosition;
    result.uv = input.uv;
    result.color = input.color;
    result.instance = material.instance.x;
    return result;
}

// Alpha coverage must be resolved before the optional pre-pass commits depth.
[shader("vertex")]
VisibilityDepthVertex depthVertexMain(VisibilityInput input)
{
    VisibilityDepthVertex result;
    precise float3 worldPosition = mul(primitiveTransforms[material.instance.x].model, float4(input.position, 1)).xyz;
    precise float4 clipPosition = mul(frame.viewProjection, float4(worldPosition, 1));
    result.position = clipPosition;
    result.uv = input.uv;
    result.color = input.color;
    return result;
}

[shader("fragment")]
void depthMain(VisibilityDepthVertex input)
{
    if (material.flags.x > 0)
    {
        readBaseColor(input.uv, input.color);
    }
}

// Opaque draws have no coverage-changing shader operations, so depth can be committed early.
[shader("fragment")][earlydepthstencil]
uint2 fragmentMain(OpaqueVisibilityVertex input, uint triangle : SV_PrimitiveID, bool frontFace : SV_IsFrontFace) : SV_Target
{
    return uint2(input.instance + 1, triangle | (frontFace ? 0 : 0x80000000u));
}

// Alpha-tested draws must reject holes before depth is committed.
[shader("fragment")]
uint2 maskedFragmentMain(VisibilityVertex input, uint triangle : SV_PrimitiveID, bool frontFace : SV_IsFrontFace) : SV_Target
{
    readBaseColor(input.uv, input.color);
    return uint2(input.instance + 1, triangle | (frontFace ? 0 : 0x80000000u));
}
