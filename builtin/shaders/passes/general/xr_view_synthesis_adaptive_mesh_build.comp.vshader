[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[comp]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#define VULTRA_SOURCE_TEXTURE sampler2DArray
#define VULTRA_SAMPLE_LOD(tex, uv, layer, lod) textureLod(tex, vec3((uv), float(layer)), lod)
#define VULTRA_TEXEL_FETCH(tex, coord, layer) texelFetch(tex, ivec3((coord), int(layer)), 0)
#else
#define VULTRA_SOURCE_TEXTURE sampler2D
#define VULTRA_SAMPLE_LOD(tex, uv, layer, lod) textureLod(tex, uv, lod)
#define VULTRA_TEXEL_FETCH(tex, coord, layer) texelFetch(tex, coord, 0)
#endif

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_Source;
layout(set = 3, binding = 1) uniform VULTRA_SOURCE_TEXTURE u_Depth;

struct XrAdaptiveMeshVertex
{
    vec4 uvDepthValid;
    vec4 color;
};

layout(set = 3, binding = 2, std430) buffer XrAdaptiveMeshVertexBuffer
{
    XrAdaptiveMeshVertex vertices[];
} s_Vertices;

layout(push_constant) uniform XrAdaptiveMeshBuildPushConstants
{
    vec2 resolution;
    uint sourceView;
    uint targetView;
    uint baseGridSize;
    uint maxSubdivision;
    float sideLengthThreshold;
    float depthThreshold;
    uint cellsX;
    uint cellsY;
    uint maxSubdiv;
    uint vertexCapacity;
} u_PC;

const uint XR_VIEW_LEFT = 1u;
const uint XR_VIEW_RIGHT = 2u;
const uint XR_VIEW_STEREO = 3u;

uint layerForSourceView()
{
    if (u_PC.sourceView == XR_VIEW_RIGHT)
        return 1u;
    return 0u;
}

vec2 pixelToUv(vec2 pixel)
{
    return clamp((pixel + vec2(0.5)) / u_PC.resolution, vec2(0.0), vec2(1.0));
}

float depthAtPixel(vec2 pixel, uint layer)
{
    ivec2 maxCoord = ivec2(u_PC.resolution) - ivec2(1);
    ivec2 coord = clamp(ivec2(pixel), ivec2(0), maxCoord);
    return clamp(VULTRA_TEXEL_FETCH(u_Depth, coord, layer).r, 0.0, 1.0);
}

float eyeOffset(uint viewKind)
{
    if (viewKind == XR_VIEW_LEFT)
        return -1.0;
    if (viewKind == XR_VIEW_RIGHT)
        return 1.0;
    return 0.0;
}

float maxTargetEyeDelta()
{
    const float srcEye = eyeOffset(u_PC.sourceView);
    if (u_PC.targetView == XR_VIEW_STEREO)
        return max(abs(-1.0 - srcEye), abs(1.0 - srcEye));
    return abs(eyeOffset(u_PC.targetView) - srcEye);
}

float warpDisparity(float depth)
{
    return (1.0 - depth) * 0.035;
}

uint nextPow2Subdiv(float demand)
{
    uint subdiv = 1u;
    while (subdiv < u_PC.maxSubdiv && float(subdiv) < demand)
        subdiv <<= 1u;
    return clamp(subdiv, 1u, max(u_PC.maxSubdiv, 1u));
}

float maxDepthDelta9(float d0, float d1, float d2, float d3, float d4, float d5, float d6, float d7, float d8)
{
    const float dMin = min(min(min(d0, d1), min(d2, d3)), min(min(d4, d5), min(d6, min(d7, d8))));
    const float dMax = max(max(max(d0, d1), max(d2, d3)), max(max(d4, d5), max(d6, max(d7, d8))));
    return dMax - dMin;
}

float maxWarpDisparityDeltaPixels9(
    float d0, float d1, float d2, float d3, float d4, float d5, float d6, float d7, float d8)
{
    const float w0 = warpDisparity(d0);
    const float w1 = warpDisparity(d1);
    const float w2 = warpDisparity(d2);
    const float w3 = warpDisparity(d3);
    const float w4 = warpDisparity(d4);
    const float w5 = warpDisparity(d5);
    const float w6 = warpDisparity(d6);
    const float w7 = warpDisparity(d7);
    const float w8 = warpDisparity(d8);
    const float dispMin = min(min(min(w0, w1), min(w2, w3)), min(min(w4, w5), min(w6, min(w7, w8))));
    const float dispMax = max(max(max(w0, w1), max(w2, w3)), max(max(w4, w5), max(w6, max(w7, w8))));
    return (dispMax - dispMin) * maxTargetEyeDelta() * u_PC.resolution.x;
}

uint chooseSubdiv(vec2 p0, vec2 p3, uint layer)
{
    const vec2 p1 = vec2(p3.x, p0.y);
    const vec2 p2 = vec2(p0.x, p3.y);
    const vec2 pm = (p0 + p3) * 0.5;
    const vec2 px = vec2(pm.x, p0.y);
    const vec2 py = vec2(p0.x, pm.y);
    const vec2 qx = vec2(pm.x, p3.y);
    const vec2 qy = vec2(p3.x, pm.y);

    float d0 = depthAtPixel(p0, layer);
    float d1 = depthAtPixel(p1, layer);
    float d2 = depthAtPixel(p2, layer);
    float d3 = depthAtPixel(p3, layer);
    float d4 = depthAtPixel(pm, layer);
    float d5 = depthAtPixel(px, layer);
    float d6 = depthAtPixel(py, layer);
    float d7 = depthAtPixel(qx, layer);
    float d8 = depthAtPixel(qy, layer);

    const float depthRange = maxDepthDelta9(d0, d1, d2, d3, d4, d5, d6, d7, d8);
    const float spanPixels = max(p3.x - p0.x, p3.y - p0.y);
    const float disparityPixels = maxWarpDisparityDeltaPixels9(d0, d1, d2, d3, d4, d5, d6, d7, d8);
    if (depthRange > max(u_PC.depthThreshold * 2.0, 1e-5) || disparityPixels > 1.0)
        return max(u_PC.maxSubdiv, 1u);

    const float targetStepPixels = max(1.0, min(spanPixels, 1.0 / max(disparityPixels, 1e-5)));
    const float depthDemand = depthRange / max(u_PC.depthThreshold * 0.5, 1e-5);
    const float disparityDemand = spanPixels / targetStepPixels;
    const float stretchDemand = (spanPixels / max(max(u_PC.resolution.x, u_PC.resolution.y), 1.0)) /
                                max(u_PC.sideLengthThreshold, 1e-5);

    return nextPow2Subdiv(max(max(depthDemand, disparityDemand), stretchDemand));
}

float quadValid(vec2 p0, vec2 p1, vec2 p2, vec2 p3, uint layer)
{
    const vec2 pm = (p0 + p3) * 0.5;
    const vec2 px = vec2(pm.x, p0.y);
    const vec2 py = vec2(p0.x, pm.y);
    const vec2 qx = vec2(pm.x, p3.y);
    const vec2 qy = vec2(p3.x, pm.y);

    const float d0 = depthAtPixel(p0, layer);
    const float d1 = depthAtPixel(p1, layer);
    const float d2 = depthAtPixel(p2, layer);
    const float d3 = depthAtPixel(p3, layer);
    const float d4 = depthAtPixel(pm, layer);
    const float d5 = depthAtPixel(px, layer);
    const float d6 = depthAtPixel(py, layer);
    const float d7 = depthAtPixel(qx, layer);
    const float d8 = depthAtPixel(qy, layer);

    const float depthRange = maxDepthDelta9(d0, d1, d2, d3, d4, d5, d6, d7, d8);
    const float disparityPixels = maxWarpDisparityDeltaPixels9(d0, d1, d2, d3, d4, d5, d6, d7, d8);
    const float stepPixels = max(max(p3.x - p0.x, p3.y - p0.y), 1.0);
    if (stepPixels <= 1.25)
        return 1.0;

    return depthRange <= max(u_PC.depthThreshold, 1e-5) && disparityPixels <= 1.0 ? 1.0 : 0.0;
}

void writeVertex(uint index, vec2 uv, float depth, uint layer, float valid)
{
    if (index >= u_PC.vertexCapacity)
        return;

    vec4 color = VULTRA_SAMPLE_LOD(u_Source, uv, layer, 0.0);
    s_Vertices.vertices[index].uvDepthValid = vec4(uv, depth, valid);
    s_Vertices.vertices[index].color = color;
}

void writeInactive(uint index)
{
    if (index >= u_PC.vertexCapacity)
        return;

    s_Vertices.vertices[index].uvDepthValid = vec4(0.0, 0.0, 1.0, 0.0);
    s_Vertices.vertices[index].color = vec4(0.0);
}

void main()
{
    const uint cellX = gl_GlobalInvocationID.x;
    const uint cellY = gl_GlobalInvocationID.y;
    if (cellX >= u_PC.cellsX || cellY >= u_PC.cellsY)
        return;

    const uint layer = layerForSourceView();
    const uint maxSubdiv = max(u_PC.maxSubdiv, 1u);
    const uint verticesPerCell = maxSubdiv * maxSubdiv * 6u;
    const uint cellBaseVertex = (cellY * u_PC.cellsX + cellX) * verticesPerCell;

    const vec2 cellMin = vec2(cellX, cellY) * float(u_PC.baseGridSize);
    const vec2 cellMax = min(cellMin + vec2(float(u_PC.baseGridSize)), u_PC.resolution - vec2(1.0));

    uint subdiv = chooseSubdiv(cellMin, cellMax, layer);
    vec2 stepPixels = max((cellMax - cellMin) / vec2(float(subdiv)), vec2(1.0));

    for (uint sy = 0u; sy < maxSubdiv; ++sy)
    {
        for (uint sx = 0u; sx < maxSubdiv; ++sx)
        {
            uint quadBase = cellBaseVertex + (sy * maxSubdiv + sx) * 6u;
            if (sx >= subdiv || sy >= subdiv)
            {
                for (uint i = 0u; i < 6u; ++i)
                    writeInactive(quadBase + i);
                continue;
            }

            vec2 p0 = cellMin + vec2(float(sx), float(sy)) * stepPixels;
            vec2 p1 = min(p0 + vec2(stepPixels.x, 0.0), cellMax);
            vec2 p2 = min(p0 + vec2(0.0, stepPixels.y), cellMax);
            vec2 p3 = min(p0 + stepPixels, cellMax);

            float qd0 = depthAtPixel(p0, layer);
            float qd1 = depthAtPixel(p1, layer);
            float qd2 = depthAtPixel(p2, layer);
            float qd3 = depthAtPixel(p3, layer);
            float valid = quadValid(p0, p1, p2, p3, layer);

            writeVertex(quadBase + 0u, pixelToUv(p0), qd0, layer, valid);
            writeVertex(quadBase + 1u, pixelToUv(p1), qd1, layer, valid);
            writeVertex(quadBase + 2u, pixelToUv(p2), qd2, layer, valid);
            writeVertex(quadBase + 3u, pixelToUv(p2), qd2, layer, valid);
            writeVertex(quadBase + 4u, pixelToUv(p1), qd1, layer, valid);
            writeVertex(quadBase + 5u, pixelToUv(p3), qd3, layer, valid);
        }
    }
}
