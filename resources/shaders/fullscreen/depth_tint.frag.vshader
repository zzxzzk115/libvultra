[vshader]
language = glsl
version = 460

[properties]
bandScale : float = 24.0 range(1.0, 128.0)
strength : float = 0.75 range(0.0, 1.0)

[frag]
layout (location = 0) in vec2 v_TexCoord;
layout (location = 0) out vec4 FragColor;

// Demonstrates multi-input fullscreen project passes: source at set=3/binding=0,
// an extra engine resource (scene depth) at set=3/binding=1.
layout (set = 3, binding = 0) uniform sampler2D t_Source;
layout (set = 3, binding = 1) uniform sampler2D t_Depth;

layout(push_constant) uniform PushConstants
{
    float bandScale;
    float strength;
};

void main() {
    vec4 src = texture(t_Source, v_TexCoord);
    float d  = texture(t_Depth, v_TexCoord).r;
    // Depth contour bands: visually proves the depth texture is bound at binding 1,
    // since the red stripes follow scene geometry by distance.
    float band = step(0.5, fract(d * max(bandScale, 1.0)));
    vec3 tinted = mix(src.rgb, vec3(1.0, 0.1, 0.1), band * strength);
    FragColor = vec4(tinted, src.a);
}
