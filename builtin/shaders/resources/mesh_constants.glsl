#ifndef MESH_CONSTANTS_GLSL
#define MESH_CONSTANTS_GLSL

struct Mesh {
    mat4 modelMatrix;
    uint materialIndex;
    uint paddingU0;
    uint paddingU1;
    uint paddingU2;
};

layout (push_constant) uniform _MeshConstants { Mesh c_Mesh; };

mat4 getModelMatrix() { return c_Mesh.modelMatrix; }
uint getMaterialIndex() { return c_Mesh.materialIndex; }

#endif