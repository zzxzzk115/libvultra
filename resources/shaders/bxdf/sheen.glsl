#ifndef VULTRA_NODE_SHEEN_GLSL
#define VULTRA_NODE_SHEEN_GLSL

// Example project BXDF/helper artifact, referenced BY NAME from a custom material
// graph node (project.bxdf.sheen) via its implementation.includes. Authoring the
// GLSL here (instead of inlining it in the node's expression) lets the node call a
// whole function. Kept self-contained so it resolves wherever it is #included.
//
// A view-dependent rim/sheen term (Fresnel-like), useful for cloth/velvet looks.
vec3 vultra_node_sheen(vec3 tint, vec3 normalWS, vec3 viewDirWS, float intensity)
{
    float ndv = clamp(dot(normalize(normalWS), normalize(viewDirWS)), 0.0, 1.0);
    float rim = pow(1.0 - ndv, 4.0);
    return tint * (rim * max(intensity, 0.0));
}

#endif // VULTRA_NODE_SHEEN_GLSL
