[vshader]
language = glsl
version = 460

[comp]
layout(local_size_x = 8, local_size_y = 8, local_size_z = 6) in;

layout(set = 0, binding = 0) uniform samplerCube u_EnvironmentMap;
layout(set = 0, binding = 1, rgba16f) writeonly uniform imageCube u_IrradianceMap;

layout(push_constant) uniform PushConstants
{
    float lodBias;
} u_Push;

const float PI = 3.1415926535897932384626433832795;
const uint SAMPLE_COUNT = 2048u;

float radicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley2d(uint i, uint n)
{
    return vec2(float(i) / float(n), radicalInverseVdC(i));
}

mat3 generateTBN(vec3 n)
{
    vec3 b = abs(dot(n, vec3(0.0, 1.0, 0.0))) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 t = normalize(cross(b, n));
    b = cross(n, t);
    return mat3(t, b, n);
}

vec4 importanceSampleLambertian(vec2 xi, vec3 n)
{
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);
    float phi = 2.0 * PI * xi.x;
    vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 l = normalize(generateTBN(n) * h);
    return vec4(l, cosTheta / PI);
}

float computeLod(float pdf, uint width, uint sampleCount)
{
    return 0.5 * log2(6.0 * float(width) * float(width) / (float(sampleCount) * max(pdf, 1e-6)));
}

vec3 directionFromCubeUV(int face, vec2 uv)
{
    uv = uv * 2.0 - 1.0;
    if (face == 0) return normalize(vec3(1.0, -uv.y, -uv.x));
    if (face == 1) return normalize(vec3(-1.0, -uv.y, uv.x));
    if (face == 2) return normalize(vec3(uv.x, 1.0, uv.y));
    if (face == 3) return normalize(vec3(uv.x, -1.0, -uv.y));
    if (face == 4) return normalize(vec3(uv.x, -uv.y, 1.0));
    return normalize(vec3(-uv.x, -uv.y, -1.0));
}

void main()
{
    ivec3 gid = ivec3(gl_GlobalInvocationID.xyz);
    ivec2 size = imageSize(u_IrradianceMap);
    if (gid.x >= size.x || gid.y >= size.y || gid.z >= 6)
        return;

    vec2 uv = (vec2(gid.xy) + 0.5) / vec2(size);
    vec3 n = directionFromCubeUV(gid.z, uv);
    uint envWidth = textureSize(u_EnvironmentMap, 0).x;

    vec3 color = vec3(0.0);
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec4 sampleData = importanceSampleLambertian(hammersley2d(i, SAMPLE_COUNT), n);
        float lod = computeLod(sampleData.w, envWidth, SAMPLE_COUNT) + u_Push.lodBias;
        color += textureLod(u_EnvironmentMap, sampleData.xyz, lod).rgb;
    }
    imageStore(u_IrradianceMap, gid, vec4(color / float(SAMPLE_COUNT), 1.0));
}
