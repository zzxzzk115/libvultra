[vshader]
language = glsl
version = 460

[properties]
exposure : float = 1.0
method : enum(KhronosPBRNeutral=0,ACES=1,Reinhard=2) = KhronosPBRNeutral

[keywords]
USE_MULTIVIEW : bool permute

[frag]
#include "include/common/color.glsl"

#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#define VULTRA_SOURCE_TEXTURE sampler2DArray
#define VULTRA_SAMPLE(tex, uv) texture(tex, vec3((uv), float(gl_ViewIndex)))
#else
#define VULTRA_SOURCE_TEXTURE sampler2D
#define VULTRA_SAMPLE(tex, uv) texture(tex, uv)
#endif

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 3, binding = 0) uniform VULTRA_SOURCE_TEXTURE u_Source;

layout(push_constant) uniform PushConstants
{
    float exposure;
    int method;
};

void main()
{
    const vec4 source = VULTRA_SAMPLE(u_Source, v_TexCoord);
    const vec3 hdr = max(source.rgb * exposure, vec3(0.0));
    FragColor = vec4(toneMapping(hdr, method), source.a);
}
