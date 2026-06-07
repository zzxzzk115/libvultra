[vshader]
id       = "builtin/general/final_composition.frag"
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute
MANUAL_SRGB_ENCODE : bool permute
DEBUG_ENTITY_ID_OUTPUT : bool permute

[frag]
#include "include/common/color.glsl"

#if USE_MULTIVIEW && !PLATFORM_WEBGPU
#extension GL_EXT_multiview : require
#endif
layout (location = 0) in vec2 v_TexCoord;
layout (location = 0) out vec4 FragColor;

#if USE_MULTIVIEW && !PLATFORM_WEBGPU
layout (set = 3, binding = 0) uniform sampler2DArray t_0;
#else
layout (set = 3, binding = 0) uniform sampler2D t_0;
#endif
#if DEBUG_ENTITY_ID_OUTPUT
layout (set = 3, binding = 1) uniform sampler2D t_EntityId;
#endif

void main() {
    vec2 sampleUv = v_TexCoord;
#if PLATFORM_WEBGPU
    sampleUv.y = 1.0 - sampleUv.y;
#endif
#if USE_MULTIVIEW && !PLATFORM_WEBGPU
    const vec4 source = texture(t_0, vec3(sampleUv, float(gl_ViewIndex)));
#else
    const vec4 source = texture(t_0, sampleUv);
#endif
#if DEBUG_ENTITY_ID_OUTPUT
    FragColor = texture(t_EntityId, sampleUv);
    return;
#endif
    vec3 color = max(source.rgb, vec3(0.0));
#if MANUAL_SRGB_ENCODE
    FragColor = vec4(linearTosRGB(color), source.a);
#else
    FragColor = vec4(color, source.a);
#endif
}
