using System;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text;
using Vultra.Interop;

[assembly: InternalsVisibleTo("ControlTest")]
[assembly: InternalsVisibleTo("Vultra.ManagedHost")]

namespace Vultra.Scripting;

public sealed class ScriptScene
{
    private readonly nint _api;
    private VultraSceneFrame _frame;
    private bool _active;

    internal unsafe ScriptScene(VultraSceneApi* api) => _api = (nint)api;

    internal Node3D CreateNode(ulong id)
    {
        return CreateNode<Node3D>(id);
    }

    internal T CreateNode<T>(ulong id) where T : Node, new()
    {
        var node = new T();
        node.Attach(this, id);
        return node;
    }

    internal void Begin(VultraSceneFrame frame)
    {
        _frame = frame;
        _active = true;
    }

    internal void End()
    {
        _active = false;
        _frame = default;
    }

    private VultraSceneFrame Frame => _active ? _frame : throw new InvalidOperationException(
        "Scene nodes can only be accessed during _Ready or _Process");

    public Node3D Root
    {
        get
        {
            ulong id;
            unsafe
            {
                Check(((VultraSceneApi*)_api)->RootId(Frame, &id));
            }
            return CreateNode(id);
        }
    }

    internal unsafe Node3D Child(ulong parent, int index)
    {
        if (index < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        ulong id;
        Check(((VultraSceneApi*)_api)->ChildId(Frame, parent, (ulong)index, &id));
        return CreateNode(id);
    }

    internal unsafe int ChildCount(ulong node)
    {
        ulong count;
        Check(((VultraSceneApi*)_api)->ChildCount(Frame, node, &count));
        return checked((int)count);
    }

    internal unsafe string Name(ulong node)
    {
        byte* data;
        ulong length;
        Check(((VultraSceneApi*)_api)->NodeName(Frame, node, &data, &length));
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(data, checked((int)length)));
    }

    internal unsafe Node3D CreateChild(ulong parent, string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateNode(Frame, parent, data, (ulong)utf8.Length, &id));
        }
        return CreateNode(id);
    }

    internal unsafe Node3D CreateMeshChild(ulong parent, string name, string assetId)
    {
        ArgumentNullException.ThrowIfNull(name);
        ArgumentNullException.ThrowIfNull(assetId);
        var nameUtf8 = Encoding.UTF8.GetBytes(name);
        var assetUtf8 = Encoding.UTF8.GetBytes(assetId);
        ulong id;
        fixed (byte* nameData = nameUtf8)
        fixed (byte* assetData = assetUtf8)
        {
            Check(((VultraSceneApi*)_api)->CreateMesh(Frame, parent,
                nameData, (ulong)nameUtf8.Length, assetData, (ulong)assetUtf8.Length, &id));
        }
        return CreateNode(id);
    }

    internal unsafe void ReparentNode(ulong node, ulong newParent)
    {
        Check(((VultraSceneApi*)_api)->ReparentNode(Frame, node, newParent));
    }

    internal unsafe Node3D DuplicateMesh(ulong source, ulong parent)
    {
        ulong id;
        Check(((VultraSceneApi*)_api)->DuplicateMesh(Frame, source, parent, &id));
        return CreateNode(id);
    }

    internal unsafe void CopyMeshModel(ulong target, ulong source)
    {
        Check(((VultraSceneApi*)_api)->CopyMeshModel(Frame, target, source));
    }

    internal unsafe void SetMeshModel(ulong node, string assetId)
    {
        ArgumentNullException.ThrowIfNull(assetId);
        var utf8 = Encoding.UTF8.GetBytes(assetId);
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->SetMeshModel(Frame, node, data, (ulong)utf8.Length));
        }
    }

    internal unsafe void RemoveNode(ulong node)
    {
        Check(((VultraSceneApi*)_api)->RemoveNode(Frame, node));
    }

    internal unsafe Camera3D CreateCameraChild(ulong parent, string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateCamera(Frame, parent, data, (ulong)utf8.Length, &id));
        }
        return CreateNode<Camera3D>(id);
    }

    internal unsafe Light3D CreateLightChild(ulong parent, string name, LightKind kind)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateLight(Frame, parent, data, (ulong)utf8.Length, (ulong)kind, &id));
        }
        return CreateNode<Light3D>(id);
    }

    internal unsafe CameraSettings CameraSettings(ulong node)
    {
        VultraCameraSettings value;
        Check(((VultraSceneApi*)_api)->CameraSettings(Frame, node, &value));
        return Scripting.CameraSettings.FromAbi(value);
    }

    internal unsafe void SetCameraSettings(ulong node, CameraSettings value)
    {
        Check(((VultraSceneApi*)_api)->SetCameraSettings(Frame, node, value.ToAbi()));
    }

    internal unsafe void SetCurrentCamera(ulong node) => Check(((VultraSceneApi*)_api)->SetCurrentCamera(Frame, node));

    public void ClearCurrentCamera() => SetCurrentCamera(0);

    public void ClearCurrentEnvironment() => SetCurrentEnvironment(0);

    internal unsafe WorldEnvironment CreateEnvironmentChild(ulong parent, string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateEnvironment(Frame, parent, data, (ulong)utf8.Length, &id));
        }
        return CreateNode<WorldEnvironment>(id);
    }

    internal unsafe EnvironmentSettings EnvironmentSettings(ulong node)
    {
        VultraEnvironmentSettings value;
        Check(((VultraSceneApi*)_api)->EnvironmentSettings(Frame, node, &value));
        return Scripting.EnvironmentSettings.FromAbi(value);
    }

    internal unsafe void SetEnvironmentSettings(ulong node, EnvironmentSettings value)
    {
        Check(((VultraSceneApi*)_api)->SetEnvironmentSettings(Frame, node, value.ToAbi()));
    }

    internal unsafe void SetEnvironmentAsset(ulong node, string assetId)
    {
        var utf8 = Encoding.UTF8.GetBytes(assetId);
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->SetEnvironmentAsset(Frame, node, data, (ulong)utf8.Length));
        }
    }

    internal unsafe void SetCurrentEnvironment(ulong node) =>
        Check(((VultraSceneApi*)_api)->SetCurrentEnvironment(Frame, node));

    public unsafe Material CreateMaterial(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateMaterial(Frame, data, (ulong)utf8.Length, &id));
        }
        return new Material(this, id);
    }

    internal unsafe Material? MeshMaterial(ulong node, int slot)
    {
        if (slot < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(slot));
        }
        ulong id;
        Check(((VultraSceneApi*)_api)->MeshMaterial(Frame, node, (ulong)slot, &id));
        return id == 0 ? null : new Material(this, id);
    }

    internal unsafe void SetMeshMaterial(ulong node, int slot, Material? material)
    {
        if (slot < 0 || (material != null && material.Scene != this))
        {
            throw new ArgumentException("Material slot must be nonnegative and resource must belong to this scene");
        }
        Check(((VultraSceneApi*)_api)->SetMeshMaterial(Frame, node, (ulong)slot, material?.Id ?? 0));
    }

    internal unsafe string MaterialName(ulong id)
    {
        byte* data;
        ulong length;
        Check(((VultraSceneApi*)_api)->MaterialName(Frame, id, &data, &length));
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(data, checked((int)length)));
    }

    internal unsafe MaterialParameters MaterialParameters(ulong id)
    {
        VultraMaterialParameters value;
        Check(((VultraSceneApi*)_api)->MaterialParameters(Frame, id, &value));
        return Scripting.MaterialParameters.FromAbi(value);
    }

    internal unsafe void SetMaterialParameters(ulong id, MaterialParameters value)
    {
        Check(((VultraSceneApi*)_api)->SetMaterialParameters(Frame, id, value.ToAbi()));
    }

    internal unsafe void RemoveMaterial(ulong id) => Check(((VultraSceneApi*)_api)->RemoveMaterial(Frame, id));

    internal unsafe LightKind LightKind(ulong node)
    {
        ulong kind;
        Check(((VultraSceneApi*)_api)->LightKind(Frame, node, &kind));
        return (LightKind)kind;
    }

    internal unsafe LightSettings LightSettings(ulong node)
    {
        VultraLightSettings value;
        Check(((VultraSceneApi*)_api)->LightSettings(Frame, node, &value));
        return Scripting.LightSettings.FromAbi(value);
    }

    internal unsafe void SetLightSettings(ulong node, LightSettings value)
    {
        Check(((VultraSceneApi*)_api)->SetLightSettings(Frame, node, value.ToAbi()));
    }

    internal unsafe Vector3 Position(ulong node)
    {
        VultraSceneTranslation value;
        Check(((VultraSceneApi*)_api)->NodeTranslation(Frame, node, &value));
        return new Vector3(value.X, value.Y, value.Z);
    }

    internal unsafe void SetPosition(ulong node, Vector3 position)
    {
        Check(((VultraSceneApi*)_api)->SetNodeTranslation(Frame, node, new VultraSceneTranslation
        {
            X = position.X,
            Y = position.Y,
            Z = position.Z
        }));
    }

    internal static void Check(VultraStatus status)
    {
        if (status != VultraStatus.Ok)
        {
            throw new InvalidOperationException($"Vultra scene API status {status}");
        }
    }
}
