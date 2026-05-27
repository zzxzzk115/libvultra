[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[frag]
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_SOURCE_TEXTURE sampler2DArray
#define VULTRA_SAMPLE_LOD(tex, uv, lod) textureLod(tex, vec3((uv), float(gl_ViewIndex)), lod)
#else
#define VULTRA_SOURCE_TEXTURE sampler2D
#define VULTRA_SAMPLE_LOD(tex, uv, lod) textureLod(tex, uv, lod)
#endif

layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_Source;

layout(push_constant) uniform PushConstants
{
    vec2 resolution;
};

#define FXAA_SPAN_MAX 16.0
#define FXAA_REDUCE_MUL (1.0 / FXAA_SPAN_MAX)
#define FXAA_REDUCE_MIN (1.0 / 64.0)
#define FXAA_SUBPIX_SHIFT (1.0 / 4.0)

void main()
{
    const vec2 rcpFrame = 1.0 / resolution;
    const vec2 uv2 = gl_FragCoord.xy / resolution;
    const vec4 uv = vec4(uv2, uv2 - (rcpFrame * (0.5 + FXAA_SUBPIX_SHIFT)));

    const vec3 rgbNW = VULTRA_SAMPLE_LOD(u_Source, uv.zw, 0.0).xyz;
    const vec3 rgbNE = VULTRA_SAMPLE_LOD(u_Source, uv.zw + vec2(1.0, 0.0) * rcpFrame.xy, 0.0).xyz;
    const vec3 rgbSW = VULTRA_SAMPLE_LOD(u_Source, uv.zw + vec2(0.0, 1.0) * rcpFrame.xy, 0.0).xyz;
    const vec3 rgbSE = VULTRA_SAMPLE_LOD(u_Source, uv.zw + vec2(1.0, 1.0) * rcpFrame.xy, 0.0).xyz;
    const vec3 rgbM = VULTRA_SAMPLE_LOD(u_Source, uv.xy, 0.0).xyz;

    const vec3 luma = vec3(0.299, 0.587, 0.114);
    const float lumaNW = dot(rgbNW, luma);
    const float lumaNE = dot(rgbNE, luma);
    const float lumaSW = dot(rgbSW, luma);
    const float lumaSE = dot(rgbSE, luma);
    const float lumaM = dot(rgbM, luma);

    const float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    const float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    const float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    const float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * rcpFrame.xy;

    const vec3 rgbA = 0.5 * (VULTRA_SAMPLE_LOD(u_Source, uv.xy + dir * (1.0 / 3.0 - 0.5), 0.0).xyz +
                             VULTRA_SAMPLE_LOD(u_Source, uv.xy + dir * (2.0 / 3.0 - 0.5), 0.0).xyz);
    const vec3 rgbB = rgbA * 0.5 + 0.25 * (VULTRA_SAMPLE_LOD(u_Source, uv.xy + dir * -0.5, 0.0).xyz +
                                           VULTRA_SAMPLE_LOD(u_Source, uv.xy + dir * 0.5, 0.0).xyz);

    const float lumaB = dot(rgbB, luma);
    FragColor = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
