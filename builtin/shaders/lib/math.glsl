#ifndef MATH_GLSL
#define MATH_GLSL

#define PI 3.1415926535897932384626433832795
#define TWO_PI 6.2831853071795864769252867665590
#define HALF_PI 1.5707963267948966192313216916398
#define EPSILON 0.00001
#define TAU 6.2831853071795864769252867665590

#define clamp01(x) clamp(x, 0.0, 1.0)

float random(vec2 co) {
    const float a = 12.9898;
    const float b = 78.233;
    const float c = 43758.5453;
    const float dt = dot(co, vec2(a, b));
    const float sn = mod(dt, PI);
    return fract(sin(sn) * c);
}

float max3(vec3 v) { return max(max(v.x, v.y), v.z); }

bool isApproximatelyEqual(float a, float b) {
  return abs(a - b) <= (abs(a) < abs(b) ? abs(b) : abs(a)) * EPSILON;
}

mat3 generateTBN(vec3 N) {
    vec3 B = vec3(0.0, 1.0, 0.0);
    float NdotUp = dot(N, vec3(0.0, 1.0, 0.0));
    if(1.0 - abs(NdotUp) <= EPSILON) {
        B = (NdotUp > 0.0) ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 0.0, -1.0);
    }
    vec3 T = normalize(cross(B, N));
    B = cross(N, T);
    return mat3(T, B, N);
}

vec3 directionFromCubeUV(int face, vec2 uv)
{
    uv = uv * 2.0 - 1.0;
    vec3 dir;
    if (face == 0) dir = vec3(1.0, -uv.y, -uv.x);       // +X
    else if (face == 1) dir = vec3(-1.0, -uv.y, uv.x);  // -X
    else if (face == 2) dir = vec3(uv.x, 1.0, uv.y);    // +Y
    else if (face == 3) dir = vec3(uv.x, -1.0, -uv.y);  // -Y
    else if (face == 4) dir = vec3(uv.x, -uv.y, 1.0);   // +Z
    else               dir = vec3(-uv.x, -uv.y, -1.0);  // -Z
    return normalize(dir);
}

// Hammersley sequence 2D
float radicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley2d(uint i, uint N)
{
    return vec2(float(i)/float(N), radicalInverse_VdC(i));
}

// Importance sample GGX microfacet normal
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // spherical to cartesian
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent space to world
    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0,0.0,1.0) : vec3(1.0,0.0,0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// Importance sample over hemisphere (for diffuse)
vec3 importanceSampleHemisphere(float u, vec3 N)
{
    float phi = 2.0 * PI * u;
    float cosTheta = sqrt(1.0 - u);
    float sinTheta = sqrt(u);

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent space to world
    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0,0.0,1.0) : vec3(1.0,0.0,0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// Smith Geometry term
float geometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Integrate BRDF for a given roughness & NoV
vec2 integrateBRDF(float NoV, float roughness, vec2 Xi)
{
    // this is a simplified version of the split-sum integral
    vec3 V;
    V.x = sqrt(1.0 - NoV*NoV);
    V.y = 0.0;
    V.z = NoV;

    vec3 N = vec3(0.0, 0.0, 1.0);
    vec3 H = importanceSampleGGX(Xi, N, roughness);
    vec3 L = normalize(2.0 * dot(V, H) * H - V);

    float NoL = max(L.z, 0.0);
    float NoH = max(H.z, 0.0);
    float VoH = max(dot(V, H), 0.0);

    if (NoL > 0.0)
    {
        float G = geometrySmith(N, V, L, roughness);
        float G_Vis = (G * VoH) / (NoH * NoV);
        float Fc = pow(1.0 - VoH, 5.0);
        return vec2((1.0 - Fc) * G_Vis, Fc * G_Vis);
    }
    return vec2(0.0);
}

#endif