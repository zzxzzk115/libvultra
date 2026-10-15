using System;
using System.Numerics;
using Vultra.Scripting;

namespace VultraModuleTests;

public sealed class Motion : Node3D
{
    private Node? _root;
    private int _readyCalls;
    private int _steps;

    public override void _Ready()
    {
        _root = Scene.Root;
        _readyCalls++;
        Position = new Vector3(1, 0, 0);
    }

    public override void _PhysicsProcess(double delta)
    {
        if (Math.Abs(delta - 1.0 / 60.0) > 1e-7 || _readyCalls != 1 || !ReferenceEquals(_root, Scene.Root))
        {
            throw new InvalidOperationException("Managed lifecycle or shared identity changed during replacement");
        }
        _steps++;
        Position = Position with { Y = _steps };
    }

    public override void _Process(double delta)
    {
#if VULTRA_MODULE_RELOAD
        Position = Position with { Z = Position.Z + 2 };
#else
        Position = Position with { Z = Position.Z + 1 };
#endif
    }

    public override void _EditorGui(EditorGui gui)
    {
        gui.Text("Managed fixed steps: ", _steps);
    }
}
