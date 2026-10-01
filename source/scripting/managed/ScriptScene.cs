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
        var node = new Node3D();
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
