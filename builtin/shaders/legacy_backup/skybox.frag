#version 460 core

layout(location = 0) in vec3 v_EyeDirection;

layout(location = 0) out vec3 FragColor;

layout(set = 3, binding = 0) uniform samplerCube t_SkyBoxCubeMap;

void main() {
	vec3 eyeDir = normalize(v_EyeDirection);
    FragColor = texture(t_SkyBoxCubeMap, eyeDir).rgb;
}