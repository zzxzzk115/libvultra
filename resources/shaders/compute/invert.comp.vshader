[vshader]
id       = "project/compute/invert.comp"
language = glsl
version = 460

[properties]
strength : float = 1.0 range(0.0, 1.0)
preserveAlpha : bool = true
mode : enum(Invert=0,Grayscale=1,AlphaMask=2) = Invert

[comp]
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 3, binding = 0) uniform texture2D u_Source;
layout(set = 3, binding = 1, rgba16f) uniform writeonly image2D u_Output;

layout(push_constant) uniform PushConstants
{
    float strength;
    int preserveAlpha;
    int mode;
};

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(u_Output);
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    vec4 color = texelFetch(u_Source, pixel, 0);
    vec3 target = vec3(1.0) - color.rgb;
    if (mode == 1)
    {
        float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
        target = vec3(1.0 - gray);
    }
    else if (mode == 2)
    {
        target = vec3(color.a);
    }
    vec3 rgb = mix(color.rgb, target, clamp(strength, 0.0, 1.0));
    imageStore(u_Output, pixel, vec4(rgb, preserveAlpha != 0 ? color.a : 1.0));
}
