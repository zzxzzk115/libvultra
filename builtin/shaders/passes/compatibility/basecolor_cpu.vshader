[vshader]
id       = "builtin/compatibility/basecolor_cpu"
language = glsl
version = 460

[keywords]
VTX_HAS_UV0 : bool permute

[vert]
// Shared by compatibility rendering and the temporary highend direct-GBuffer fallback.
#include "include/common/cpu_scene.glsl"

#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 0
#endif

layout(location = 0) in vec3 a_Position;
#if VTX_HAS_UV0
layout(location = 3) in vec2 a_TexCoord0;
#endif

layout(location = 0) out vec2 v_TexCoord0;

void main()
{
    vec4 worldPos = u_Draw.model * vec4(a_Position, 1.0);
    gl_Position = u_CameraBlock.data.viewProjection * worldPos;
#if VTX_HAS_UV0
    v_TexCoord0 = a_TexCoord0;
#else
    v_TexCoord0 = vec2(0.0);
#endif
}

[frag]
#include "include/common/cpu_scene.glsl"

#ifndef VTX_HAS_UV0
#define VTX_HAS_UV0 0
#endif

layout(location = 0) in vec2 v_TexCoord0;
layout(location = 0) out vec4 FragColor;
layout(set = 3, binding = 4) uniform sampler2D u_CompatBaseColorTexture;

void main()
{
    FragColor = u_Draw.baseColorFactor;
#if VTX_HAS_UV0
    FragColor *= texture(u_CompatBaseColorTexture, v_TexCoord0);
#endif
}
