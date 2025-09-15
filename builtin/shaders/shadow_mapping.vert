#version 460 core

#include "resources/light_block.glsl"
#include "resources/mesh_constants.glsl"

layout (location = 0) in vec3 a_Position;

void main() {
    gl_Position = getLightSpaceMatrix() * getModelMatrix() * vec4(a_Position, 1.0);
}