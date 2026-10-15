using System;
using System.IO;
using System.Numerics;
using Vultra.Scripting;

namespace VultraTests;

public sealed class IdentityProbe : Node3D
{
    private Node? _root;
    private int _readyCalls;
    private string _exitMarker = "";

    public override void _Ready()
    {
        _readyCalls++;
        if (GetProperty<string>("name") != Name || GetProperty<float[]>("localTransform")?.Length != 16)
        {
            throw new InvalidOperationException("Generated object properties differ from managed node access");
        }
        SetProperty("processPriority", 0);
        _root = Scene.Root;
        _exitMarker = Path.Combine("build/.tmp", Scene.Root.Name + "-exits.txt");
    }

    public override void _Process(double delta)
    {
        if (_readyCalls != 1 || !ReferenceEquals(_root, Scene.Root))
        {
            throw new InvalidOperationException("Reload reran Ready or lost scene-entry state");
        }
        var first  = Scene.Root.GetChild<IdentityProbe>(0);
        var second = Scene.Root.GetChild<IdentityProbe>(1);
        if (!ReferenceEquals(first.Scene, second.Scene) || !ReferenceEquals(first.Scene.Root, second.Scene.Root) ||
            !ReferenceEquals(first, Scene.Root.GetChild(0)))
        {
            throw new InvalidOperationException("Scripts do not share scene object identities");
        }
#if VULTRA_IDENTITY_RELOAD
        Position = new Vector3(ReferenceEquals(this, first) ? 3 : 4, 0, 0);
#else
        Position = new Vector3(ReferenceEquals(this, first) ? 1 : 2, 0, 0);
#endif
    }
    public override void _ExitTree()
    {
        File.AppendAllText(_exitMarker, "x");
    }
}
