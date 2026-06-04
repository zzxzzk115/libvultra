#pragma once

namespace vultra
{
    // Collision shape built from the entity's MeshComponent geometry.
    //   convex == false : static triangle mesh (level/terrain geometry; not for dynamic bodies).
    //   convex == true  : convex hull (usable on dynamic bodies).
    // The mesh's CPU vertex/index data is baked with the entity's transform scale so the
    // collider matches the rendered mesh.
    struct MeshShapeComponent
    {
        bool convex {false};
    };
} // namespace vultra
