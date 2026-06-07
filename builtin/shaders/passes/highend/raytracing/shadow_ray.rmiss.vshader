[vshader]
id       = "builtin/highend/shadow_ray.rmiss"
language = glsl
version = 460

[rmiss]
#extension GL_EXT_ray_tracing : enable

layout(location = 1) rayPayloadInEXT bool shadowed;

void main()
{
    shadowed = false;
}
