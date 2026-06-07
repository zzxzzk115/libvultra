[vshader]
id       = "builtin/highend/default_rt_primary.rahit"
language = glsl
version = 460

[rahit]
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_GOOGLE_include_directive : require

#define VULTRA_SCENE_SET 2
#define VULTRA_MATERIAL_TABLE_BINDING 1
#define VULTRA_MATERIAL_PARAMS_BINDING 4
#define VULTRA_TEXTURE_SET 3
#define VULTRA_BINDLESS_TEXTURES_BINDING 4
#define VULTRA_DECLARE_MATERIAL_TABLE
#define VULTRA_DECLARE_MATERIAL_PARAMS
#define VULTRA_DECLARE_BINDLESS_TEXTURES
#include "include/common/gpu_scene.glsl"

layout(set = 1, binding = 0, std140) uniform Camera
{
    CameraData data;
} u_CameraBlock;

struct GPUInstanceData
{
    uint geometryOffset;
    uint geometryCount;
    uint materialOffset;
    uint materialCount;
};
layout(std430, set = 2, binding = 0) readonly buffer InstanceData { GPUInstanceData instances[]; };

struct GPUGeometryNode
{
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint vertexOffset;
    uint materialIndex;
    uint vertexStrideBytes;
    uint positionOffsetBytes;
    uint normalOffsetBytes;
    uint texCoord0OffsetBytes;
    uint tangentOffsetBytes;
};
layout(std430, set = 2, binding = 2) readonly buffer GeometryNodes { GPUGeometryNode geometryNodes[]; };

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer IndexBuffer { uint indices[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer FloatBuffer { float values[]; };

hitAttributeEXT vec2 attribs;

const uint INVALID_OFFSET = 0xFFFFFFFFu;
const uint ALPHA_MODE_MASK = 1u;

vec3 loadVec3(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, vec3 fallback)
{
    if (offsetBytes == INVALID_OFFSET)
        return fallback;

    FloatBuffer data = FloatBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return vec3(data.values[0], data.values[1], data.values[2]);
}

vec2 loadVec2(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, vec2 fallback)
{
    if (offsetBytes == INVALID_OFFSET)
        return fallback;

    FloatBuffer data = FloatBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return vec2(data.values[0], data.values[1]);
}

float rcp(float v) { return 1.0 / v; }
vec3 rcp(vec3 v) { return vec3(1.0 / v.x, 1.0 / v.y, 1.0 / v.z); }
vec3 getRow0(mat3x2 m) { return vec3(m[0][0], m[1][0], m[2][0]); }
vec3 getRow1(mat3x2 m) { return vec3(m[0][1], m[1][1], m[2][1]); }

struct BarycentricDeriv
{
    vec3 lambda;
    vec3 ddx;
    vec3 ddy;
};

struct UvGrad
{
    vec2 uv;
    vec2 dx;
    vec2 dy;
};

BarycentricDeriv calcFullBary(vec4 pt0, vec4 pt1, vec4 pt2, vec2 pixelNdc, vec2 twoOverWindowSize)
{
    BarycentricDeriv ret;
    vec3 invW = rcp(vec3(pt0.w, pt1.w, pt2.w));
    vec2 ndc0 = pt0.xy * invW.x;
    vec2 ndc1 = pt1.xy * invW.y;
    vec2 ndc2 = pt2.xy * invW.z;

    float invDet = rcp(determinant(mat2(ndc2 - ndc1, ndc0 - ndc1)));
    ret.ddx = vec3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.ddy = vec3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;

    float ddxSum = dot(ret.ddx, vec3(1.0));
    float ddySum = dot(ret.ddy, vec3(1.0));
    vec2 delta = pixelNdc - ndc0;
    float interpInvW = invW.x + delta.x * ddxSum + delta.y * ddySum;
    float interpW = rcp(interpInvW);

    ret.lambda.x = interpW * (invW.x + delta.x * ret.ddx.x + delta.y * ret.ddy.x);
    ret.lambda.y = interpW * (0.0 + delta.x * ret.ddx.y + delta.y * ret.ddy.y);
    ret.lambda.z = interpW * (0.0 + delta.x * ret.ddx.z + delta.y * ret.ddy.z);

    ret.ddx *= twoOverWindowSize.x;
    ret.ddy *= twoOverWindowSize.y;
    ddxSum *= twoOverWindowSize.x;
    ddySum *= twoOverWindowSize.y;

    ret.ddy *= -1.0;
    ddySum *= -1.0;

    float interpWddx = rcp(interpInvW + ddxSum);
    float interpWddy = rcp(interpInvW + ddySum);
    ret.ddx = interpWddx * (ret.lambda * interpInvW + ret.ddx) - ret.lambda;
    ret.ddy = interpWddy * (ret.lambda * interpInvW + ret.ddy) - ret.lambda;
    return ret;
}

UvGrad calcUvGrad(vec3 p0, vec3 p1, vec3 p2, vec2 uv0, vec2 uv1, vec2 uv2)
{
    vec2 pixelNdc = (vec2(gl_LaunchIDEXT.xy) + vec2(0.5)) / vec2(gl_LaunchSizeEXT.xy) * 2.0 - 1.0;
    vec2 twoOverWindowSize = 2.0 / u_CameraBlock.data.resolution.xy;
    BarycentricDeriv deriv = calcFullBary(u_CameraBlock.data.viewProjection * vec4(p0, 1.0),
                                          u_CameraBlock.data.viewProjection * vec4(p1, 1.0),
                                          u_CameraBlock.data.viewProjection * vec4(p2, 1.0),
                                          pixelNdc,
                                          twoOverWindowSize);
    mat3x2 uvs = mat3x2(uv0, uv1, uv2);
    vec3 row0 = getRow0(uvs);
    vec3 row1 = getRow1(uvs);

    UvGrad outGrad;
    outGrad.uv = vec2(dot(row0, deriv.lambda), dot(row1, deriv.lambda));
    outGrad.dx = vec2(dot(row0, deriv.ddx), dot(row1, deriv.ddx));
    outGrad.dy = vec2(dot(row0, deriv.ddy), dot(row1, deriv.ddy));
    return outGrad;
}

void main()
{
    const uint instanceIndex = gl_InstanceCustomIndexEXT;
    GPUInstanceData instance = instances[nonuniformEXT(instanceIndex)];

    const uint geomGlobalIndex = instance.geometryOffset + gl_GeometryIndexEXT;
    GPUGeometryNode node = geometryNodes[nonuniformEXT(geomGlobalIndex)];
    const uint materialIndex = instance.materialOffset + node.materialIndex;
    if (get_material_model(materialIndex) != VULTRA_MAT_PBRMR)
        return;

    MaterialParamsPBRMR material = get_pbrmr_params(materialIndex);
    if (material.alphaMode != ALPHA_MODE_MASK)
        return;

    IndexBuffer ib = IndexBuffer(node.indexBufferAddress);
    const uint i0 = node.vertexOffset + ib.indices[gl_PrimitiveID * 3 + 0];
    const uint i1 = node.vertexOffset + ib.indices[gl_PrimitiveID * 3 + 1];
    const uint i2 = node.vertexOffset + ib.indices[gl_PrimitiveID * 3 + 2];

    const vec3 p0 = loadVec3(node.vertexBufferAddress, i0, node.vertexStrideBytes, node.positionOffsetBytes, vec3(0.0));
    const vec3 p1 = loadVec3(node.vertexBufferAddress, i1, node.vertexStrideBytes, node.positionOffsetBytes, vec3(0.0));
    const vec3 p2 = loadVec3(node.vertexBufferAddress, i2, node.vertexStrideBytes, node.positionOffsetBytes, vec3(0.0));
    const vec3 p0WS = (gl_ObjectToWorldEXT * vec4(p0, 1.0)).xyz;
    const vec3 p1WS = (gl_ObjectToWorldEXT * vec4(p1, 1.0)).xyz;
    const vec3 p2WS = (gl_ObjectToWorldEXT * vec4(p2, 1.0)).xyz;

    const vec2 uv0 = loadVec2(node.vertexBufferAddress, i0, node.vertexStrideBytes, node.texCoord0OffsetBytes, vec2(0.0));
    const vec2 uv1 = loadVec2(node.vertexBufferAddress, i1, node.vertexStrideBytes, node.texCoord0OffsetBytes, vec2(0.0));
    const vec2 uv2 = loadVec2(node.vertexBufferAddress, i2, node.vertexStrideBytes, node.texCoord0OffsetBytes, vec2(0.0));
    UvGrad uvGrad = calcUvGrad(p0WS, p1WS, p2WS, uv0, uv1, uv2);

    vec4 baseColor = material.baseColor;
    if (material.baseColorTex != 0u)
    {
        baseColor *= textureGrad(getBindlessTexture(material.baseColorTex),
                                 uvGrad.uv,
                                 uvGrad.dx,
                                 uvGrad.dy);
    }
    if (baseColor.a < material.alphaCutoff)
        ignoreIntersectionEXT;
}
