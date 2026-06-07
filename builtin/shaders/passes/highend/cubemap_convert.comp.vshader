[vshader]
id       = "builtin/highend/cubemap_convert.comp"
language = glsl
version = 460

[comp]
layout(local_size_x = 8, local_size_y = 8, local_size_z = 6) in;

layout(set = 0, binding = 0) uniform sampler2D u_EquirectMap;
layout(set = 0, binding = 1, rgba16f) writeonly uniform imageCube u_OutputCube;

const float PI = 3.1415926535897932384626433832795;
const float TAU = 6.2831853071795864769252867665590;

vec3 cubeCoordToWorld(ivec3 cubeCoord, float cubemapSize)
{
    vec2 uv = vec2(cubeCoord.xy) / cubemapSize;
    uv = uv * 2.0 - 1.0;
    if (cubeCoord.z == 0) return vec3(1.0, -uv.y, -uv.x);
    if (cubeCoord.z == 1) return vec3(-1.0, -uv.y, uv.x);
    if (cubeCoord.z == 2) return vec3(uv.x, 1.0, uv.y);
    if (cubeCoord.z == 3) return vec3(uv.x, -1.0, -uv.y);
    if (cubeCoord.z == 4) return vec3(uv.x, -uv.y, 1.0);
    return vec3(-uv.x, -uv.y, -1.0);
}

vec2 sampleSphericalMap(vec3 dir)
{
    vec2 uv = vec2(atan(dir.z, dir.x), asin(clamp(dir.y, -1.0, 1.0)));
    uv *= vec2(1.0 / TAU, 1.0 / PI);
    return uv + 0.5;
}

void main()
{
    ivec3 gid = ivec3(gl_GlobalInvocationID);
    ivec2 size = imageSize(u_OutputCube).xy;
    if (gid.x >= size.x || gid.y >= size.y)
        return;

    vec3 dir = normalize(cubeCoordToWorld(gid, float(size.x)));
    vec2 uv = sampleSphericalMap(dir);
    uv.y = 1.0 - uv.y;
    imageStore(u_OutputCube, gid, vec4(texture(u_EquirectMap, uv).rgb, 1.0));
}
