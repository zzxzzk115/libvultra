[vshader]
language = glsl
version = 460

[keywords]
USE_SCENE_DEPTH : bool permute

[frag]
#ifndef USE_SCENE_DEPTH
#define USE_SCENE_DEPTH 0
#endif

layout(location = 0) in vec2 v_TexCoord;

layout(set = 0, binding = 0, rg32f) uniform readonly image2D t_DepthTransmittance;
#if USE_SCENE_DEPTH
layout(set = 0, binding = 1) uniform sampler2D t_SceneDepth;
#endif

void main()
{
    ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
    vec2 depthTrans  = imageLoad(t_DepthTransmittance, pixelCoord).rg;
    float pickedDepth = depthTrans.r;

    bool hasDepth = false;
    float resolvedDepth = 1.0;

#if USE_SCENE_DEPTH
    resolvedDepth = texture(t_SceneDepth, v_TexCoord).r;
    hasDepth = true;
#endif

    if (pickedDepth > 0.0001)
    {
        resolvedDepth = hasDepth ? min(resolvedDepth, pickedDepth) : pickedDepth;
        hasDepth = true;
    }

    if (!hasDepth)
        discard;

    gl_FragDepth = resolvedDepth;
}
