[vshader]
language = glsl
version = 460

[frag]
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

// Meshlet (opaque scene) color — set=3, binding=0
layout(set = 3, binding = 0) uniform sampler2D t_Meshlet;
// Gaussian splat color (premultiplied alpha) — set=3, binding=1
layout(set = 3, binding = 1) uniform sampler2D t_Splat;

void main()
{
    vec4 meshlet = texture(t_Meshlet, v_TexCoord);
    vec4 splat   = texture(t_Splat,   v_TexCoord);

    // Premultiplied-alpha over: splat layer on top of opaque meshlet layer.
    // splat.rgb is already premultiplied by alpha in the gaussian render pass.
    vec3 blended = splat.rgb + meshlet.rgb * (1.0 - splat.a);
    FragColor = vec4(blended, 1.0);
}
