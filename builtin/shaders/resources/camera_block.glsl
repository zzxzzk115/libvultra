#ifndef CAMERA_BLOCK_GLSL
#define CAMERA_BLOCK_GLSL

struct Camera {
    mat4  projection;
    mat4  inversedProjection;
    mat4  view;
    mat4  inversedView;
    mat4  viewProjection;
    mat4  inversedViewProjection;
    uvec2 resolution;
    uvec2 padding0;
    float near;
    float far;
    float fovY;
    float padding1;
    vec4  frustumPlanes[6];
};

layout (set = 1, binding = 0, std140) uniform _CameraBlock { Camera u_Camera; };

vec3 getCameraPosition() { return u_Camera.inversedView[3].xyz; }

uvec2 getResolution() { return u_Camera.resolution; }
vec2 getScreenTexelSize() { return 1.0 / vec2(u_Camera.resolution); }

float getAspectRatio() {
    return float(u_Camera.resolution.x) / float(u_Camera.resolution.y);
}

#endif