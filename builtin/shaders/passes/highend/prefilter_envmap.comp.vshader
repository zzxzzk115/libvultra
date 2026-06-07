[vshader]
id       = "builtin/highend/prefilter_envmap.comp"
language = glsl
version = 460

[comp]
layout(local_size_x = 8, local_size_y = 8, local_size_z = 6) in;

layout(set = 0, binding = 0) uniform samplerCube u_EnvironmentMap;
layout(set = 0, binding = 1, rgba16f) writeonly uniform imageCube u_PrefilteredMap;

layout(push_constant) uniform PushConstants
{
    uint mipLevel;
    float roughness;
    uint sampleCount;
} u_Push;

const float PI = 3.1415926535897932384626433832795;

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

float distributionGGXTangent(float cosTheta, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = cosTheta * cosTheta * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

vec4 importanceSampleGGX(vec2 xi, vec3 n, vec3 v, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 h = normalize(generateTBN(n) * vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta));
    float noH = max(dot(n, h), 0.0);
    float voH = max(dot(v, h), 0.0);
    float d = distributionGGXTangent(cosTheta, roughness);
    float pdf = max(d * noH / (4.0 * voH), 1e-6);
    return vec4(h, pdf);
}

float computeLod(float pdf, uint width, uint sampleCount)
{
    return 0.5 * log2(6.0 * float(width) * float(width) / (float(sampleCount) * max(pdf, 1e-6)));
}

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

void main()
{
    uint mipImageSize = imageSize(u_PrefilteredMap).x;
    ivec3 coord = ivec3(gl_GlobalInvocationID.xyz);
    if (coord.x >= mipImageSize || coord.y >= mipImageSize || coord.z >= 6)
        return;

    vec3 n = normalize(cubeCoordToWorld(coord, float(mipImageSize)));
    vec3 v = n;
    uint width = textureSize(u_EnvironmentMap, 0).x;

    vec3 color = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < u_Push.sampleCount; ++i)
    {
        vec4 sampleData = importanceSampleGGX(hammersley2d(i, u_Push.sampleCount), n, v, u_Push.roughness);
        vec3 l = normalize(reflect(-v, sampleData.xyz));
        float nDotL = max(dot(n, l), 0.0);
        if (nDotL > 0.0)
        {
            color += textureLod(u_EnvironmentMap, l, computeLod(sampleData.w, width, u_Push.sampleCount)).rgb * nDotL;
            weight += nDotL;
        }
    }
    imageStore(u_PrefilteredMap, coord, vec4(color / max(weight, 1e-6), 1.0));
}
