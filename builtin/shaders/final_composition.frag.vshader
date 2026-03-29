[vshader]
language = glsl
version = 460

[keywords]
USE_MULTIVIEW : bool permute

[frag]
#if USE_MULTIVIEW
#extension GL_EXT_multiview : require
#endif
layout (location = 0) in vec2 v_TexCoord;
layout (location = 0) out vec4 FragColor;

#if USE_MULTIVIEW
layout (set = 3, binding = 0) uniform sampler2DArray t_0;
#else
layout (set = 3, binding = 0) uniform sampler2D t_0;
#endif

void main() {
#if USE_MULTIVIEW
    const vec4 source = texture(t_0, vec3(v_TexCoord, float(gl_ViewIndex)));
#else
    const vec4 source = texture(t_0, v_TexCoord);
#endif
	FragColor = vec4(source.rgb, 1.0);
}
