#version 460 core

#include "resources/frame_block.glsl"
#include "resources/camera_block.glsl"
#include "resources/light_block.glsl"
#include "resources/mesh_constants.glsl"

layout (location = 0) in vec3 a_Position;
layout (location = 1) in vec3 a_Color;
layout (location = 2) in vec3 a_Normal;
layout (location = 3) in vec2 a_TexCoords;
layout (location = 5) in vec4 a_Tangent;

layout (location = 0) out vec3 v_Color;
layout (location = 1) out vec2 v_TexCoord;
layout (location = 2) out vec3 v_FragPos;
layout (location = 3) out mat3 v_TBN;

void main() {
    v_Color = a_Color;
    v_TexCoord = a_TexCoords;
    v_FragPos = vec3(getModelMatrix() * vec4(a_Position, 1.0));
    vec3 normal = transpose(mat3(getModelMatrix())) * a_Normal;
    v_TBN = mat3(a_Tangent.xyz, cross(a_Tangent.xyz, normal) * a_Tangent.w, normal);

    gl_Position = u_Camera.viewProjection * vec4(v_FragPos, 1.0);
}