using System.Numerics;
using Vultra.Interop;

namespace Vultra.Scripting;

public sealed class Material
{
    internal ScriptScene Scene { get; }
    internal ulong Id { get; }

    internal Material(ScriptScene scene, ulong id)
    {
        Scene = scene;
        Id = id;
    }

    public string Name => Scene.MaterialName(Id);
    public MaterialParameters Parameters
    {
        get => Scene.MaterialParameters(Id);
        set => Scene.SetMaterialParameters(Id, value);
    }
    public void Remove() => Scene.RemoveMaterial(Id);
}

// Value snapshot; changes reach the shared resource when assigned back to Material.Parameters.
public readonly record struct MaterialParameters
{
    public Vector4 BaseColor { get; init; } = Vector4.One;
    public float BaseWeight { get; init; } = 1;
    public float Metalness { get; init; }
    public float DiffuseRoughness { get; init; }
    public float SpecularWeight { get; init; } = 1;
    public Vector3 SpecularColor { get; init; } = Vector3.One;
    public float Roughness { get; init; } = 0.3f;
    public float SpecularIor { get; init; } = 1.5f;
    public float CoatWeight { get; init; }
    public float CoatRoughness { get; init; } = 0.1f;
    public float CoatIor { get; init; } = 1.5f;
    public Vector3 EmissionColor { get; init; }
    public float EmissionLuminance { get; init; } = 1;
    public float NormalScale { get; init; } = 1;
    public float OcclusionStrength { get; init; } = 1;
    public float AlphaCutoff { get; init; } = -1;

    public MaterialParameters() { }

    internal static MaterialParameters FromAbi(VultraMaterialParameters value) => new()
    {
        BaseColor = new Vector4(value.BaseRed, value.BaseGreen, value.BaseBlue, value.BaseAlpha),
        BaseWeight = value.BaseWeight,
        Metalness = value.BaseMetalness,
        DiffuseRoughness = value.BaseDiffuseRoughness,
        SpecularWeight = value.SpecularWeight,
        SpecularColor = new Vector3(value.SpecularRed, value.SpecularGreen, value.SpecularBlue),
        Roughness = value.SpecularRoughness,
        SpecularIor = value.SpecularIor,
        CoatWeight = value.CoatWeight,
        CoatRoughness = value.CoatRoughness,
        CoatIor = value.CoatIor,
        EmissionColor = new Vector3(value.EmissionRed, value.EmissionGreen, value.EmissionBlue),
        EmissionLuminance = value.EmissionLuminance,
        NormalScale = value.NormalScale,
        OcclusionStrength = value.OcclusionStrength,
        AlphaCutoff = value.AlphaCutoff
    };

    internal VultraMaterialParameters ToAbi() => new()
    {
        BaseRed = BaseColor.X,
        BaseGreen = BaseColor.Y,
        BaseBlue = BaseColor.Z,
        BaseAlpha = BaseColor.W,
        BaseWeight = BaseWeight,
        BaseMetalness = Metalness,
        BaseDiffuseRoughness = DiffuseRoughness,
        SpecularWeight = SpecularWeight,
        SpecularRed = SpecularColor.X,
        SpecularGreen = SpecularColor.Y,
        SpecularBlue = SpecularColor.Z,
        SpecularRoughness = Roughness,
        SpecularIor = SpecularIor,
        CoatWeight = CoatWeight,
        CoatRoughness = CoatRoughness,
        CoatIor = CoatIor,
        EmissionRed = EmissionColor.X,
        EmissionGreen = EmissionColor.Y,
        EmissionBlue = EmissionColor.Z,
        EmissionLuminance = EmissionLuminance,
        NormalScale = NormalScale,
        OcclusionStrength = OcclusionStrength,
        AlphaCutoff = AlphaCutoff
    };
}
