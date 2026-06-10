[vshader]
id       = "builtin/general/pullpush_push.frag"
language = glsl
version = 460

[keywords]
USE_MULTIVIEW   : bool permute
USE_DEPTH_AWARE : bool permute

[frag]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_SOURCE_TEXTURE sampler2DArray
#define VULTRA_SAMPLE_LOD(tex, uv, lod) textureLod(tex, vec3((uv), float(gl_ViewIndex)), lod)
#define VULTRA_TEXTURE_SIZE(tex, lod) textureSize(tex, lod).xy
#else
#define VULTRA_SOURCE_TEXTURE sampler2D
#define VULTRA_SAMPLE_LOD(tex, uv, lod) textureLod(tex, uv, lod)
#define VULTRA_TEXTURE_SIZE(tex, lod) textureSize(tex, lod)
#endif

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_Pyramid;

layout(push_constant) uniform XrPullPushConstants
{
    int   lod;
    float depthThreshold;
} u_PC;

#define INDEX_COUNT 2
#define TOTAL_COUNT (INDEX_COUNT * INDEX_COUNT)

int sampleOffset(int i) { return 2 * i + 1 - INDEX_COUNT; }

// Alpha-validity convention (shared with the geometry warp):
//   [0, 0.5]  valid    (depth = a * 2)
//   (0.5, 1)  invalid  (depth = (a - 0.5) * 2)
//   1.0       hole
const float kAlphaClassEps = 1.0 / 255.0;

#if USE_DEPTH_AWARE
float encodeValidAlpha(float d) { return clamp(d * 0.5, 0.0, 0.5 - kAlphaClassEps); }
float encodeInvalidAlpha(float d) { return clamp(d * 0.5 + 0.5, 0.5 + kAlphaClassEps, 1.0 - kAlphaClassEps); }
#endif

void main()
{
    const ivec2 texSize   = max(VULTRA_TEXTURE_SIZE(u_Pyramid, u_PC.lod), ivec2(1));
    const vec2  texelSize = 1.0 / vec2(texSize);

    vec4 colors[TOTAL_COUNT];
    for (int y = 0; y < INDEX_COUNT; ++y)
        for (int x = 0; x < INDEX_COUNT; ++x)
        {
            const vec2 uv             = v_TexCoord + texelSize * 0.5 * vec2(sampleOffset(x), sampleOffset(y));
            colors[y * INDEX_COUNT + x] = VULTRA_SAMPLE_LOD(u_Pyramid, uv, float(u_PC.lod));
        }

    vec4 accum = vec4(0.0);
    int  count = 0;

#if USE_DEPTH_AWARE
    // Keep the nearest surface: average valid samples within depthThreshold of the
    // nearest (max) depth, so a foreground edge does not bleed into the background.
    float maxDepth = 0.0;
    for (int i = 0; i < TOTAL_COUNT; ++i)
    {
        const float a = colors[i].a;
        const float d = (a < 0.5) ? a * 2.0 : (a - 0.5) * 2.0;
        if (d < 1.0)
            maxDepth = max(maxDepth, d);
    }
    for (int i = 0; i < TOTAL_COUNT; ++i)
    {
        const float a = colors[i].a;
        if (a < 0.5 && (maxDepth - a * 2.0) < u_PC.depthThreshold)
        {
            accum += colors[i];
            ++count;
        }
    }
    if (count > 0)
        accum /= float(count);
    accum.a = (count == 0) ? encodeInvalidAlpha(maxDepth) : encodeValidAlpha(maxDepth);
#else
    for (int i = 0; i < TOTAL_COUNT; ++i)
        if (colors[i].a < 1.0)
        {
            accum += colors[i];
            ++count;
        }
    if (count > 0)
        accum /= float(count);
    accum.a = (count == 0) ? 1.0 : 0.0;
#endif

    FragColor = accum;
}
