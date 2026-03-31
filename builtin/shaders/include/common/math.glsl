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

float rcp(float v) {
    return 1.0 / v;
}

vec2 rcp(vec2 v) {
    return vec2(1.0 / v.x, 1.0 / v.y);
}

vec3 rcp(vec3 v) {
    return vec3(1.0 / v.x, 1.0 / v.y, 1.0 / v.z);
}

vec4 rcp(vec4 v) {
    return vec4(1.0 / v.x, 1.0 / v.y, 1.0 / v.z, 1.0 / v.w);
}

// Column-major order
vec3 getCol0(mat3 m) { return m[0]; }
vec3 getCol1(mat3 m) { return m[1]; }
vec3 getCol2(mat3 m) { return m[2]; }

vec3 getRow0(mat3x2 m) { return vec3(m[0][0], m[1][0], m[2][0]); }
vec3 getRow1(mat3x2 m) { return vec3(m[0][1], m[1][1], m[2][1]); }

#endif