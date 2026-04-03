[vshader]
language = glsl
version = 460

[vert]
layout(location = 0) out vec4 v_Color;

void main()
{
    const vec2 positions[3] = vec2[](
        vec2(-0.6, -0.5),
        vec2(0.6, -0.5),
        vec2(0.0, 0.6)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    v_Color     = vec4(0.95, 0.65, 0.15, 1.0);
}
