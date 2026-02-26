#version 460 core

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform sampler2D t_Point;

layout(push_constant) uniform _PushConstants { int lod; };

#ifndef INDEX_COUNT
#define INDEX_COUNT 2
#endif

#define TOTAL_COUNT (INDEX_COUNT * INDEX_COUNT)

// Compute offset for index i (0..INDEX_COUNT-1)
// Example: INDEX_COUNT=2 -> {-1, 1}
//          INDEX_COUNT=4 -> {-3, -1, 1, 3}
//          INDEX_COUNT=8 -> {-7, -5, -3, -1, 1, 3, 5, 7}
int getOffset(int i) {
    return 2 * i + 1 - INDEX_COUNT;
}

#ifndef DEPTH_THRESHOLD
#define DEPTH_THRESHOLD 1.0
#endif

void main() {
    ivec2 texSize = textureSize(t_Point, lod);
    vec2 texelSize = vec2(1.0 / texSize.x, 1.0 / texSize.y);

#if USE_DEPTH_AWARE
    float maxDepth = 0.0;
#if USE_COMPUTE_WARPING
    vec4 maxDepthColor = vec4(0.0);
    int nonOneDepthCount = 0;
#endif
#endif

    int validCount = 0;
    vec4 res = vec4(0.0);

    vec4 colors[TOTAL_COUNT];

    for (int i = 0; i < INDEX_COUNT; ++i)
    {
        for (int j = 0; j < INDEX_COUNT; ++j)
        {
            vec2 sampleUV = v_TexCoord + vec2(texelSize.x / 2.0 * getOffset(i), texelSize.y / 2.0 * getOffset(j));
            colors[(i * INDEX_COUNT) + j] = textureLod(t_Point, sampleUV, lod);
        }
    }

    bool valid[TOTAL_COUNT];
    for (int i = 0; i < TOTAL_COUNT; ++i)
    {
#if USE_DEPTH_AWARE
    valid[i] = colors[i].a < 0.5;
#else
    valid[i] = colors[i].a < 1.0;
#endif
    }

#if USE_DEPTH_AWARE
    float depths[TOTAL_COUNT];
    for (int i = 0; i < TOTAL_COUNT; i++) {
        depths[i] = valid[i] ?  colors[i].a * 2 : (colors[i].a - 0.5) * 2;
#if USE_COMPUTE_WARPING
        nonOneDepthCount += (depths[i] < 1.0) ? 1 : 0;
#endif
    }

    for (int i = 0; i < TOTAL_COUNT; i++) {

        if (depths[i] > maxDepth && depths[i] < 1.0)
        {
            maxDepth = depths[i];
#if USE_COMPUTE_WARPING
            maxDepthColor = colors[i];
#endif
        }
    }
#endif

    for (int i = 0; i < TOTAL_COUNT; ++i)
    {
#if USE_DEPTH_AWARE && !USE_COMPUTE_WARPING
        if (valid[i] && maxDepth - depths[i] < DEPTH_THRESHOLD)
#else
        if (valid[i])
#endif
        {
            validCount++;
            res += colors[i];
        }
    }

    if (validCount > 0)
    {
        res /= validCount;
    }

#if USE_DEPTH_AWARE
#if USE_COMPUTE_WARPING
    if (validCount == TOTAL_COUNT)
    {
        // All pixels are valid
        // -> encode in valid range [0.0, 0.5]
        res.a = maxDepth * 0.5;
        res.rgb = maxDepthColor.rgb;
    }
    else if (validCount == 0)
    {
        if (nonOneDepthCount == TOTAL_COUNT)
        {
            // No valid pixels, but all have depth values
            // encode in valid range [0.0, 0.5]
            res.a = maxDepth * 0.5;
            res.rgb = maxDepthColor.rgb;
        }
        else if (nonOneDepthCount > 0)
        {
            // No valid pixels, but at least one valid depth value
            // -> encode in invalid range (0.5, 1.0]
            res.a = maxDepth * 0.5 + 0.5;
            res.rgb = maxDepthColor.rgb;
        }
        else
        {
            // No valid pixels and no depth at all -> hole, encode as 1.0 (hole)
            res.a = 1.0;
        }
    }
    // Mixed pixels （some valid, some invalid)
    else
    {
        if (nonOneDepthCount == TOTAL_COUNT)
        {
            // All have depth values
            // -> encode in valid range [0.0, 0.5]
            res.a = maxDepth * 0.5;
            res.rgb = maxDepthColor.rgb;
        }
        else
        {
            // But at least one pixel has no depth at all
            // -> encode in invalid range (0.5, 1.0]
            res.a = maxDepth * 0.5 + 0.5;
            res.rgb = maxDepthColor.rgb;
        }
    }
#else
    res.a = (maxDepth / 2) + (validCount == 0 ? 0.5 : 0);
#endif
#else
    res.a = validCount == 0 ? 1.0 : 0.0;
#endif

    FragColor = res;
}