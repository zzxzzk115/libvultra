using System;
using Vultra.Scripting;

namespace VultraScript;

public sealed partial class ThrottleController : Node3D
{
    public override void _Process(double deltaSeconds) =>
        throw new InvalidOperationException("Reloaded C# code was executed");
}
