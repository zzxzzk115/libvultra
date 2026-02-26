#ifndef CUBEMAP_GLSL
#define CUBEMAP_GLSL

#include "math.glsl"

#define POSITIVE_X 0
#define NEGATIVE_X 1
#define POSITIVE_Y 2
#define NEGATIVE_Y 3
#define POSITIVE_Z 4
#define NEGATIVE_Z 5

vec3 getCubeFace(int face, vec2 uv) {
	// clang-format off
	switch (face) {
		case POSITIVE_X: return vec3(1.0, -uv.yx);       
		case NEGATIVE_X: return vec3(-1.0, -uv.y, uv.x); 
		case POSITIVE_Y: return vec3(uv.x, 1.0, uv.y);  
		case NEGATIVE_Y: return vec3(uv.x, -1.0, -uv.y); 
		case POSITIVE_Z: return vec3(uv.x, -uv.y, 1.0);  
		case NEGATIVE_Z: return vec3(-uv.xy, -1.0);      
	}
	// clang-format on
	return vec3(0.0);
}

vec3 cubeCoordToWorld(ivec3 cubeCoord, float cubemapSize) {
	const vec2 uv = vec2(cubeCoord.xy) / cubemapSize;
	return getCubeFace(cubeCoord.z, uv * 2.0 - 1.0);
}

vec2 sampleSphericalMap(vec3 dir) {
	vec2 v = vec2(atan(dir.z, dir.x), asin(dir.y));
	v *= vec2(1.0 / TAU, 1.0 / PI); // -> [-0.5, 0.5]
	return v + 0.5;                 // -> [0.0, 1.0]
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

#endif