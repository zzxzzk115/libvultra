[vshader]
id       = "builtin/general/xr_view_synthesis_push.frag"
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

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
    int lod;
} u_PC;

#define INDEX_COUNT 2
#define TOTAL_COUNT (INDEX_COUNT * INDEX_COUNT)

int sampleOffset(int i)
{
    return 2 * i + 1 - INDEX_COUNT;
}

void main()
{
    const ivec2 texSize = max(VULTRA_TEXTURE_SIZE(u_Pyramid, u_PC.lod), ivec2(1));
    const vec2 texelSize = 1.0 / vec2(texSize);

    int validCount = 0;
    vec4 colorAccum = vec4(0.0);

    for (int y = 0; y < INDEX_COUNT; ++y)
    {
        for (int x = 0; x < INDEX_COUNT; ++x)
        {
            const vec2 sampleUv = v_TexCoord + texelSize * 0.5 * vec2(sampleOffset(x), sampleOffset(y));
            const vec4 sampleValue = VULTRA_SAMPLE_LOD(u_Pyramid, sampleUv, float(u_PC.lod));
            if (sampleValue.a > 0.5)
            {
                colorAccum += sampleValue;
                ++validCount;
            }
        }
    }

    if (validCount > 0)
        FragColor = colorAccum / float(validCount);
    else
        FragColor = vec4(0.0);
}
