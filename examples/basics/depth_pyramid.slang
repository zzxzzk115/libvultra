// A depth pyramid: a procedural depth image, its mip chain of 2x2 maxima and a view of every level.

struct Parameters
{
    uint2 sourceSize;
    uint2 targetSize;
    float time;
    uint  levels; // Display: the number of levels in the pyramid.
};

[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

// Compute passes read one level (or the depth buffer) from `source` and write the next to `target`; the display
// reads the whole pyramid from `source`.
[[vk::binding(0, 0)]] Texture2D<float>   source;
[[vk::binding(1, 0)]] RWTexture2D<float> target;

struct FullscreenOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
FullscreenOutput fullscreen(uint vertex : SV_VertexID)
{
    const float2     uv = float2((vertex << 1) & 2, vertex & 2);
    FullscreenOutput output;
    output.position = float4(uv * 2 - 1, 0, 1);
    output.uv       = uv;
    return output;
}

// Depth of a few spheres moving in front of a far plane.
[shader("fragment")]
float scene(FullscreenOutput input) : SV_Depth
{
    float depth = 0.95;
    for (uint index = 0; index < 4; ++index)
    {
        const float  phase  = parameters.time + float(index) * 1.7;
        const float2 center = float2(0.5 + 0.3 * cos(phase), 0.5 + 0.3 * sin(phase * 1.3));
        const float  radius = 0.08 + 0.04 * float(index);
        const float  extent = length(input.uv - center) / radius;
        if (extent < 1)
        {
            depth = min(depth, 0.2 + 0.15 * float(index) + 0.1 * extent * extent);
        }
    }
    return depth;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void copyDepth(uint3 id : SV_DispatchThreadID)
{
    if (all(id.xy < parameters.targetSize))
    {
        target[id.xy] = source.Load(int3(id.xy, 0));
    }
}

// Each texel of a level is the maximum of its 2x2 footprint in the level above. Where the level above has an
// odd width or height, the last column or row of the level also covers the source texels left over, so every
// source texel is represented and the pyramid stays conservative.
[shader("compute")]
[numthreads(8, 8, 1)]
void reduce(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= parameters.targetSize))
    {
        return;
    }
    const uint2 first = id.xy * 2;
    uint2       last  = first + 1;
    if (id.x + 1 == parameters.targetSize.x)
    {
        last.x = parameters.sourceSize.x - 1;
    }
    if (id.y + 1 == parameters.targetSize.y)
    {
        last.y = parameters.sourceSize.y - 1;
    }
    last         = min(last, parameters.sourceSize - 1);
    float result = 0;
    for (uint y = first.y; y <= last.y; ++y)
    {
        for (uint x = first.x; x <= last.x; ++x)
        {
            result = max(result, source.Load(int3(x, y, 0)));
        }
    }
    target[id.xy] = result;
}

// The left half shows level 0; the right half stacks levels 1, 2, ... from the top at their own resolution.
[shader("fragment")]
float4 display(FullscreenOutput input) : SV_Target
{
    const uint2 pixel = uint2(input.position.xy);
    const uint  half  = parameters.sourceSize.x / 2;
    uint        level = 0;
    uint2       texel = pixel;
    if (pixel.x >= half)
    {
        texel.x   = pixel.x - half;
        uint top  = 0;
        level     = 1;
        bool found = false;
        for (; level < parameters.levels && !found; ++level)
        {
            const uint2 size = max(parameters.sourceSize >> level, uint2(1, 1));
            if (pixel.y < top + size.y)
            {
                if (texel.x >= size.x)
                {
                    return float4(0.05, 0.05, 0.08, 1);
                }
                texel.y = pixel.y - top;
                found   = true;
                break;
            }
            top += size.y;
        }
        if (!found)
        {
            return float4(0.05, 0.05, 0.08, 1);
        }
    }
    const float value = source.Load(int3(texel, level));
    return float4(value.xxx, 1);
}
