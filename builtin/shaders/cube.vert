#version 460 core

layout(push_constant) uniform _PushConstants { mat4 VP; };

out gl_PerVertex {
    vec4 gl_Position;
};
layout(location = 0) out vec3 v_FragPos;

void main()
{
    // https://gist.github.com/rikusalminen/9393151
    const int tri = gl_VertexIndex / 3;
    const int idx = gl_VertexIndex % 3;
    const int face = tri / 2;
    const int top = tri % 2;

    const int dir = face % 3;
    const int pos = face / 3;

    const int nz = dir >> 1;
    const int ny = dir & 1;
    const int nx = 1 ^ (ny | nz);

    const vec3 d = vec3(nx, ny, nz);
    const float flip = 1.0 - 2.0 * pos;

    const vec3 n = flip * d;
    const vec3 u = -d.yzx;
    const vec3 v = flip * d.zxy;

    const float mirror = -1.0 + 2.0 * top;
    const vec3 xyz = n + mirror * (1.0 - 2.0 * (idx & 1)) * u +
                     mirror * (1.0 - 2.0 * (idx >> 1)) * v;

    v_FragPos = xyz;
    gl_Position = VP * vec4(v_FragPos, 1.0);
}