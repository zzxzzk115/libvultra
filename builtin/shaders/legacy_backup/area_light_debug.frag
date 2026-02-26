#version 460 core

layout(location = 0) in vec4 v_DebugColor;

layout (location = 0) out vec3 g_Albedo;
layout (location = 1) out vec3 g_Normal;
layout (location = 2) out vec3 g_Emissive;
layout (location = 3) out vec3 g_MetallicRoughnessAO;

void main() {
    g_Albedo = vec3(0.0);
    g_Normal = vec3(0.0, 0.0, 0.0); // special value indicating area light (not a surface)
    g_Emissive = v_DebugColor.rgb;
    g_MetallicRoughnessAO = vec3(0.0, 1.0, 0.0);
}