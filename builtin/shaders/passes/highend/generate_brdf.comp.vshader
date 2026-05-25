[vshader]
language = glsl
version = 460

[comp]
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) writeonly uniform image2D u_BrdfLUT;

const float PI = 3.1415926535897932384626433832795;
const uint SAMPLE_COUNT = 1024u;

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

float distributionGGXTangent(float cosTheta, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = cosTheta * cosTheta * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float vSmithGGXCorrelated(float noV, float noL, float roughness)
{
    float a2 = pow(roughness, 4.0);
    float ggxV = noL * sqrt(noV * noV * (1.0 - a2) + a2);
    float ggxL = noV * sqrt(noL * noL * (1.0 - a2) + a2);
    return 0.5 / (ggxV + ggxL);
}

mat3 generateTBN(vec3 n)
{
    vec3 b = abs(dot(n, vec3(0.0, 1.0, 0.0))) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 t = normalize(cross(b, n));
    b = cross(n, t);
    return mat3(t, b, n);
}

vec3 importanceSampleGGX(vec2 xi, vec3 n, vec3 v, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return normalize(generateTBN(n) * h);
}

vec2 integrateBRDF(float nDotV, float roughness)
{
    vec3 v = vec3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
    vec3 n = vec3(0.0, 0.0, 1.0);
    float a = 0.0;
    float b = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec3 h = importanceSampleGGX(hammersley2d(i, SAMPLE_COUNT), n, v, roughness);
        vec3 l = normalize(reflect(-v, h));
        float nDotL = clamp(l.z, 0.0, 1.0);
        float nDotH = clamp(h.z, 0.0, 1.0);
        float vDotH = clamp(dot(v, h), 0.0, 1.0);
        if (nDotL > 0.0)
        {
            float gVis = vSmithGGXCorrelated(nDotV, nDotL, roughness) * vDotH * nDotL / nDotH;
            float fc = pow(1.0 - vDotH, 5.0);
            a += (1.0 - fc) * gVis;
            b += fc * gVis;
        }
    }
    return vec2(4.0 * a, 4.0 * b) / float(SAMPLE_COUNT);
}

void main()
{
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(u_BrdfLUT);
    if (gid.x >= size.x || gid.y >= size.y)
        return;

    vec2 uv = (vec2(gid) + 0.5) / vec2(size);
    vec2 result = integrateBRDF(uv.x, uv.y);
    imageStore(u_BrdfLUT, gid, vec4(result, 0.0, 1.0));
}
