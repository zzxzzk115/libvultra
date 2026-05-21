[vshader]
language = glsl
version = 460

[vert]
struct ShadowCascadeData
{
    mat4 lightViewProjection;
    vec4 atlasScaleOffset;
    vec4 splitDepth;
};

layout(set = 0, binding = 0) uniform ShadowData
{
    ShadowCascadeData cascades[4];
    vec4 lightDirectionDepthBias;
    vec4 shadowParams;
    vec4 cascadeParams;
} u_Shadow;

layout(push_constant) uniform DrawParams
{
    mat4 model;
    uvec4 cascadeIndex;
} u_Draw;

layout(location = 0) in vec3 a_Position;

void main()
{
    uint cascade = min(u_Draw.cascadeIndex.x, 3u);
    gl_Position = u_Shadow.cascades[cascade].lightViewProjection * u_Draw.model * vec4(a_Position, 1.0);
}
