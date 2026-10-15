using System;
using System.Numerics;
using System.Text.Json;

namespace Vultra.Scripting;

public readonly record struct ActionState(bool Held, bool Pressed, bool Released);

public enum ProcessMode : ulong
{
    Inherit,
    Pausable,
    WhenPaused,
    Always,
    Disabled
}

// A script instance is attached to one scene node.
public class Node
{
    private ScriptScene? _scene;
    private ulong _id;

    public ScriptScene Scene => _scene ?? throw new InvalidOperationException("Node is not attached to a scene");
    public string      Name  => Scene.Name(_id);
    public Node? Parent      => Scene.Parent(_id);
    public Node? Owner
    {
        get => Scene.Owner(_id);
        set => Scene.SetOwner(_id, value);
    }
    public int  ChildCount          => Scene.ChildCount(_id);
    public T? GetProperty<T>(string name) => JsonSerializer.Deserialize<T>(Scene.ReadPropertyJson(_id, name));
    public void SetProperty<T>(string name, T value) => Scene.WritePropertyJson(_id, name, JsonSerializer.Serialize(value));
    public Node GetChild(int index) => Scene.Child(_id, index);
    public T    GetChild<T>(int index)
        where   T : Node => GetChild(index) as T ?? throw new InvalidCastException($"Child is not a {typeof(T).Name}");
    public Node CreateChild(string name)                                  => Scene.CreateChild(_id, name);
    public Node3D           CreateChild3D(string name)                    => Scene.CreateChild3D(_id, name);
    public MeshInstance3D   CreateMeshChild(string name, string assetId)  => Scene.CreateMeshChild(_id, name, assetId);
    public Camera3D         CreateCameraChild(string name)                => Scene.CreateCameraChild(_id, name);
    public Light3D          CreateLightChild(string name, LightKind kind) => Scene.CreateLightChild(_id, name, kind);
    public WorldEnvironment CreateEnvironmentChild(string name)           => Scene.CreateEnvironmentChild(_id, name);

    public void Reparent(Node newParent, bool keepGlobalTransform = true)
    {
        if (!ReferenceEquals(Scene, newParent.Scene))
        {
            throw new ArgumentException("Parent belongs to another world");
        }
        Scene.ReparentNode(_id, newParent.Id, keepGlobalTransform);
    }

    public void Remove() => Scene.RemoveNode(_id);
    public void QueueFree() => Scene.QueueFree(_id);
    public bool IsInsideTree => Scene.InsideTree(_id);
    public bool IsQueuedForDeletion => Scene.QueuedForDeletion(_id);
    public ProcessMode ProcessMode
    {
        get => Scene.ProcessMode(_id);
        set => Scene.SetProcessMode(_id, value);
    }
    public int ProcessPriority
    {
        get => Scene.ProcessPriority(_id);
        set => Scene.SetProcessPriority(_id, value);
    }

    internal ulong Id => _id;

    internal void Attach(ScriptScene scene, ulong id)
    {
        _scene = scene;
        _id    = id;
    }

    internal void Detach() => _scene = null;

    public virtual void _EnterTree()
    {
    }

    public virtual void _Ready()
    {
    }

    public virtual void _Process(double delta)
    {
    }

    public virtual void _PhysicsProcess(double delta)
    {
    }

    public virtual void _EditorGui(EditorGui gui)
    {
    }

    public virtual void _ExitTree()
    {
    }
}

public class Node3D : Node
{
    public Vector3 Position
    {
        get => Scene.Position(Id);
        set => Scene.SetPosition(Id, value);
    }
}

public class MeshInstance3D : Node3D
{
    public MeshInstance3D DuplicateMesh(Node parent)           => Scene.DuplicateMesh(Id, parent.Id);
    public void           CopyMeshModel(MeshInstance3D source) => Scene.CopyMeshModel(Id, source.Id);
    public void           SetMeshModel(string assetId)         => Scene.SetMeshModel(Id, assetId);
    public Material? GetMaterial(int slot)                     => Scene.MeshMaterial(Id, slot);
    public void SetMaterial(int slot, Material? material)      => Scene.SetMeshMaterial(Id, slot, material);
}

public enum LightKind : ulong
{
    Directional,
    Point,
    Spot
}

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
    public void MakeCurrent()                     => Scene.SetCurrentEnvironment(Id);
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
