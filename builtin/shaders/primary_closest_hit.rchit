#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

#include "lib/color.glsl"
#include "lib/bda_vertex.glsl"

layout(set = 3, binding = 0) uniform accelerationStructureEXT topLevelAS;

struct GPUMaterial {
    uint albedoIndex;
};
layout(set = 2, binding = 0) buffer Materials { GPUMaterial materials[]; };

struct GPUGeometryNode {
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint materialIndex;
};
layout(set = 2, binding = 1) buffer GeometryNodes { GPUGeometryNode geometryNodes[]; };

layout(set = 2, binding = 2) uniform sampler2D textures[512]; // TODO: Variable length

layout(push_constant) uniform GlobalPushConstants
{
    vec4 missColor;
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 1) rayPayloadEXT bool shadowed;
hitAttributeEXT vec2 attribs;

void main()
{
	const uint geomIndex = gl_GeometryIndexEXT;
    GPUGeometryNode node = geometryNodes[nonuniformEXT(geomIndex)];
    GPUMaterial mat = materials[nonuniformEXT(node.materialIndex)];

	Vertex v0 = fromBufferDeviceAddresses(node.vertexBufferAddress, node.indexBufferAddress, 0);
	Vertex v1 = fromBufferDeviceAddresses(node.vertexBufferAddress, node.indexBufferAddress, 1);
	Vertex v2 = fromBufferDeviceAddresses(node.vertexBufferAddress, node.indexBufferAddress, 2);

	vec2 uv = (1.0 - attribs.x - attribs.y) * v0.texCoord + attribs.x * v1.texCoord + attribs.y * v2.texCoord;

    float dist = length(gl_WorldRayOriginEXT + gl_HitTEXT * gl_WorldRayDirectionEXT 
                    - gl_WorldRayOriginEXT); // hit distance
	float lod = log2(dist * 0.25);
	vec3 albedo = textureLod(textures[nonuniformEXT(mat.albedoIndex)], uv, lod).rgb;

    hitValue = linearTosRGB(toneMappingKhronosPbrNeutral(albedo));
	// hitValue = vec3(float(mat.albedoIndex) / 255.0);
}