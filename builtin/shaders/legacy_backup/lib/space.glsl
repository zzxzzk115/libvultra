#ifndef SPACE_GLSL
#define SPACE_GLSL

vec3 clipToView(vec4 v, mat4 inversedProjection) {
    const vec4 view = inversedProjection * v; // Transform clip space to view space
    return view.xyz / view.w; // Divide by w to convert from homogeneous coordinates
}

vec3 viewToWorld(vec4 v, mat4 inversedView) {
    return vec3(inversedView * v);
}

#endif