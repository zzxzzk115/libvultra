using System.Numerics;
using Vultra.Scripting;

namespace VultraTests;

public sealed class LightingController : Node3D
{
    private Light3D _light = null!;
    private Camera3D _camera = null!;
    private Material _material = null!;
    private WorldEnvironment _environment = null!;

    public override void _Ready()
    {
        _light = CreateLightChild("Script light", LightKind.Point);
        _light.Position = new Vector3(0, 0, 2);
        _light.Settings = _light.Settings with { Color = new Vector3(0.2f, 0.6f, 1) };
        _camera = CreateCameraChild("Script camera");
        _camera.Position = new Vector3(0, 0, 3);
        _camera.MakeCurrent();
        _material = Scene.CreateMaterial("Script material");
        _material.Parameters = _material.Parameters with { BaseColor = new Vector4(1, 0.25f, 0.1f, 1) };
        GetChild(0).SetMaterial(0, _material);
        _environment = CreateEnvironmentChild("Script environment");
        _environment.MakeCurrent();
    }

    public override void _Process(double delta)
    {
        _light.Settings = _light.Settings with { Intensity = 2 + (float)delta };
        _camera.Settings = _camera.Settings with { VerticalFov = 0.9f };
        _material.Parameters = _material.Parameters with { BaseColor = new Vector4(0.4f + (float)delta, 0.25f, 0.1f, 1) };
        _environment.Settings = new EnvironmentSettings(1 + (float)delta);
    }
}
