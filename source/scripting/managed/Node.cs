using System;
using System.Numerics;

namespace Vultra.Scripting;

// A script instance is attached to one scene node.
public class Node
{
    private ScriptScene? _scene;
    private ulong _id;

    public ScriptScene Scene => _scene ?? throw new InvalidOperationException("Node is not attached to a scene");
    public string Name => Scene.Name(_id);
    public int ChildCount => Scene.ChildCount(_id);
    public Node3D GetChild(int index) => Scene.Child(_id, index);
    public Node3D CreateChild(string name) => Scene.CreateChild(_id, name);
    public Node3D CreateMeshChild(string name, string assetId) => Scene.CreateMeshChild(_id, name, assetId);
    public Camera3D CreateCameraChild(string name) => Scene.CreateCameraChild(_id, name);
    public Light3D CreateLightChild(string name, LightKind kind) => Scene.CreateLightChild(_id, name, kind);
    public WorldEnvironment CreateEnvironmentChild(string name) => Scene.CreateEnvironmentChild(_id, name);
    public void Reparent(Node newParent) => Scene.ReparentNode(_id, newParent.Id);
    public void Remove() => Scene.RemoveNode(_id);

    internal ulong Id => _id;

    internal void Attach(ScriptScene scene, ulong id)
    {
        _scene = scene;
        _id = id;
    }

    public virtual void _Ready() { }
    public virtual void _Process(double delta) { }
    public virtual void _EditorGui(EditorGui gui) { }
    public virtual void _ExitTree() { }
}

public class Node3D : Node
{
    public Camera3D AsCamera()
    {
        Scene.CameraSettings(Id);
        return Scene.CreateNode<Camera3D>(Id);
    }

    public Light3D AsLight()
    {
        Scene.LightKind(Id);
        return Scene.CreateNode<Light3D>(Id);
    }

    public WorldEnvironment AsEnvironment()
    {
        Scene.EnvironmentSettings(Id);
        return Scene.CreateNode<WorldEnvironment>(Id);
    }

    public Node3D DuplicateMesh(Node parent) => Scene.DuplicateMesh(Id, parent.Id);
    public void CopyMeshModel(Node3D source) => Scene.CopyMeshModel(Id, source.Id);
    public void SetMeshModel(string assetId) => Scene.SetMeshModel(Id, assetId);
    public Material? GetMaterial(int slot) => Scene.MeshMaterial(Id, slot);
    public void SetMaterial(int slot, Material? material) => Scene.SetMeshMaterial(Id, slot, material);

    public Vector3 Position
    {
        get => Scene.Position(Id);
        set => Scene.SetPosition(Id, value);
    }
}

public enum LightKind : ulong
{
    Directional,
    Point,
    Spot
}

public readonly record struct CameraSettings(float VerticalFov, float NearPlane, float FarPlane);
public readonly record struct EnvironmentSettings(float Intensity);
public readonly record struct LightSettings(Vector3 Color, float Intensity, float Range, float InnerCone, float OuterCone);

public class Camera3D : Node3D
{
    public CameraSettings Settings
    {
        get => Scene.CameraSettings(Id);
        set => Scene.SetCameraSettings(Id, value);
    }

    public void MakeCurrent() => Scene.SetCurrentCamera(Id);
}

// The environment is non-spatial; its parent and local transforms do not affect lighting.
public class WorldEnvironment : Node
{
    public EnvironmentSettings Settings
    {
        get => Scene.EnvironmentSettings(Id);
        set => Scene.SetEnvironmentSettings(Id, value);
    }

    public void SetRadianceAsset(string? assetId) => Scene.SetEnvironmentAsset(Id, assetId ?? "");
    public void MakeCurrent() => Scene.SetCurrentEnvironment(Id);
}

public class Light3D : Node3D
{
    public LightKind Kind => Scene.LightKind(Id);
    public LightSettings Settings
    {
        get => Scene.LightSettings(Id);
        set => Scene.SetLightSettings(Id, value);
    }
}
