[vshader]
id       = "builtin/highend/skybox.frag"
language = glsl
version = 460

[frag]
layout(location = 0) in vec3 v_EyeDirection;
layout(location = 0) out vec4 o_Color;

layout(set = 3, binding = 0) uniform samplerCube u_SkyboxCubeMap;

void main()
{
    vec3 dir = normalize(v_EyeDirection);
    o_Color = vec4(texture(u_SkyboxCubeMap, dir).rgb, 1.0);
}
