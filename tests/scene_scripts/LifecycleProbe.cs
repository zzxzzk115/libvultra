using System;
using System.IO;
using System.Numerics;
using Vultra.Scripting;

namespace VultraTests;

public sealed class LifecycleProbe : Node3D
{
    private void Record(string phase) =>
        File.AppendAllText(Path.Combine("build/.tmp", Scene.Root.Name + ".txt"), Name + ":" + phase + "\n");

    public override void _EnterTree() => Record("enter");

    public override void _Ready()
    {
        Position += Vector3.UnitX;
        ProcessPriority = -7;
        Scene.TimeScale = Scene.TimeScale;
        if (!IsInsideTree || ProcessPriority != -7 || Scene.TimeScale != 1)
        {
            throw new InvalidOperationException("Managed scene phase/priority ABI is inconsistent");
        }
        Record("ready");
    }

    public override void _PhysicsProcess(double delta)
    {
        if (Math.Abs(delta - 1.0 / 60.0) > 1e-7 || Scene.PhysicsFrame == 0)
        {
            throw new InvalidOperationException("Managed fixed update has no scene clock");
        }
        var action = Scene.Action("jump");
        if (!action.Held || action.Pressed != (Scene.PhysicsFrame == 1))
        {
            throw new InvalidOperationException("Managed fixed input repeated or lost its edge");
        }
        Position += Vector3.UnitY;
        Record("fixed");
    }

    public override void _Process(double delta)
    {
        Position += Vector3.UnitZ;
        Record("process");
    }

    public override void _ExitTree()
    {
        if (!IsInsideTree)
        {
            throw new InvalidOperationException("Managed exit lost tree access");
        }
        Record("exit");
    }
}
