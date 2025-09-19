#ifndef COLOR_GLSL
#define COLOR_GLSL

#include "math.glsl"

const float kGamma = 2.4;
const float kInvGamma = 1.0 / kGamma;

vec3 linearTosRGB(vec3 color) {
    #if 1
  const bvec3 cutoff = lessThan(color, vec3(0.0031308));
    const vec3 higher = 1.055 * pow(color, vec3(kInvGamma)) - 0.055;
    const vec3 lower = color * 12.92;
    return mix(higher, lower, cutoff);
    #else
  return pow(color, vec3(kInvGamma));
    #endif
}
vec4 linearTosRGB(vec4 color) { return vec4(linearTosRGB(color.rgb), color.a); }
vec3 sRGBToLinear(vec3 color) {
    #if 1
  const bvec3 cutoff = lessThan(color, vec3(0.04045));
    const vec3 higher = pow((color + 0.055) / 1.055, vec3(kGamma));
    const vec3 lower = color / 12.92;
    return mix(higher, lower, cutoff);
    #else
  return vec3(pow(color, vec3(kGamma)));
    #endif
}
vec4 sRGBToLinear(vec4 color) { return vec4(sRGBToLinear(color.rgb), color.a); }

vec3 toneMappingKhronosPbrNeutral(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    const float d = 1. - startCompression;
    float newPeak = 1. - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
    return mix(color, newPeak * vec3(1, 1, 1), g);
}

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 toneMappingACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp01((x * (a * x + b)) / (x * (c * x + d) + e));
}

vec3 toneMappingReinhard(vec3 color) {
    return color / (color + vec3(1.0));
}

vec3 toneMapping(vec3 color, int method) {
    if (method == 0) {
        return toneMappingKhronosPbrNeutral(color);
    } else if (method == 1) {
        return toneMappingACES(color);
    } else if (method == 2) {
        return toneMappingReinhard(color);
    } else {
        return toneMappingKhronosPbrNeutral(color); // Fallback to Khronos PBR Neutral
    }
}

#endif