[vshader]
id       = "builtin/general/pullpush_pull.frag"
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
#else
#define VULTRA_SOURCE_TEXTURE sampler2D
#define VULTRA_SAMPLE_LOD(tex, uv, lod) textureLod(tex, uv, lod)
#endif

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_LinearPyramid; // color propagation
layout(set = 3, binding = 1) uniform VULTRA_SOURCE_TEXTURE u_PointPyramid;  // classification

layout(push_constant) uniform XrPullPushConstants
{
    int   lod;
    float depthThreshold;
} u_PC;

const float kAlphaClassEps = 1.0 / 255.0;

void main()
{
    const vec4 current    = VULTRA_SAMPLE_LOD(u_PointPyramid, v_TexCoord, float(u_PC.lod));
    const vec4 nextPoint  = VULTRA_SAMPLE_LOD(u_PointPyramid, v_TexCoord, float(u_PC.lod + 1));
    const vec4 nextLinear = VULTRA_SAMPLE_LOD(u_LinearPyramid, v_TexCoord, float(u_PC.lod + 1));

    // Replace invalid/hole pixels with the coarser (already-repaired) level; keep
    // valid pixels untouched. Linear color from the coarser level smooths the fill.
#if USE_DEPTH_AWARE
    const bool replace = current.a > 0.5;                  // invalid or hole
#else
    const bool replace = current.a >= 1.0 - kAlphaClassEps; // hole
#endif

    if (replace)
        FragColor = vec4((u_PC.lod == 0) ? nextPoint.rgb : nextLinear.rgb, nextPoint.a);
    else
        FragColor = current;
}
