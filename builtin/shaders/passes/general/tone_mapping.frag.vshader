[vshader]
language = glsl
version = 460

[properties]
exposure : float = 1.0
method : enum(KhronosPBRNeutral=0,ACES=1,Reinhard=2) = KhronosPBRNeutral

[frag]
#include "include/common/color.glsl"

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform sampler2D u_Source;

layout(push_constant) uniform PushConstants
{
    float exposure;
    int method;
};

void main()
{
    const vec4 source = texture(u_Source, v_TexCoord);
    const vec3 hdr = max(source.rgb * exposure, vec3(0.0));
    FragColor = vec4(toneMapping(hdr, method), source.a);
}
