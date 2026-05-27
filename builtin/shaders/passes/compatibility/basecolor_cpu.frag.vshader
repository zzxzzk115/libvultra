[vshader]
language = glsl
version = 460

[keywords]
VTX_HAS_UV0 : bool permute

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
