[vshader]
language = glsl
version = 460

[keywords]
USE_DEPTH_AWARE : bool permute

[frag]
#ifndef USE_DEPTH_AWARE
#define USE_DEPTH_AWARE 1
#endif

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

// Meshlet (opaque scene) color — set=3, binding=0
layout(set = 3, binding = 0) uniform sampler2D t_Meshlet;
// Gaussian splat color (premultiplied alpha) — set=3, binding=1
layout(set = 3, binding = 1) uniform sampler2D t_Splat;
#if USE_DEPTH_AWARE
// Gaussian splat accumulated depth/coverage — set=3, binding=2
layout(set = 3, binding = 2) uniform sampler2D t_SplatDepthAccum;
// Opaque scene depth-pre — set=3, binding=3
layout(set = 3, binding = 3) uniform sampler2D t_SceneDepth;
#endif

void main()
{
#if USE_DEPTH_AWARE
    const float kDepthEpsilon = 1e-4;
#endif

    vec4 meshlet    = texture(t_Meshlet, v_TexCoord);
    vec4 splat      = texture(t_Splat, v_TexCoord);
#if USE_DEPTH_AWARE
    vec4 depthAccum = texture(t_SplatDepthAccum, v_TexCoord);
    float sceneDepth = texture(t_SceneDepth, v_TexCoord).r;

    float splatOpacity = depthAccum.a;
    float splatDepth   = splatOpacity > 1e-6 ? (depthAccum.r / splatOpacity) : 1.0;

    if (splatOpacity <= 1e-6)
    {
        FragColor = meshlet;
        return;
    }

    if (sceneDepth < splatDepth - kDepthEpsilon)
    {
        FragColor = meshlet;
        return;
    }
#endif

    // Front-to-back splat color is already accumulated in premultiplied form.
    // Mesh shows through only via remaining transmittance.
    vec3 blended = splat.rgb + meshlet.rgb * (1.0 - splat.a);
    float alpha = splat.a + meshlet.a * (1.0 - splat.a);
    FragColor = vec4(blended, alpha);
}
