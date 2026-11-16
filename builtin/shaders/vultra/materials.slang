// `import vultra.materials;` evaluates the material types a program knows by their numbers, the `type` of a draw
// (MaterialDraw) or of a scene.materials record. This file knows only vultra.openpbr (number 0), so a program built
// with it pays nothing for dispatch. A host that uses more types generates its own vultra/materials.slang with the
// same three functions and puts its directory before builtin/shaders among the include roots.
import vultra.material;

// The surface of the bound material (MaterialDraw::instance.z gives its type).
VultraSurface vultraMaterialSurface(VultraSurfaceInput input)
{
    VultraOpenPbr type;
    return type.surface(input);
}

// Whether a type's standard G-buffer layers are enough to light it; a type that answers false is lit through
// vultraMaterialShade().
bool vultraMaterialStandard(uint type)
{
    return true;
}

// The shade() of the material `header` names, with its parameters read from scene.material_data.
float3 vultraMaterialShade(StructuredBuffer<uint> data,
                           VultraMaterialHeader   header,
                           VultraSurface          surface,
                           float3                 toLight,
                           float3                 toView)
{
    VultraOpenPbr type;
    return type.shade(surface, toLight, toView);
}
