using System;
using System.Numerics;
using Vultra.Scripting;

namespace VultraScript;

public sealed partial class ThrottleController : Node3D
{
    private int _pendingStep;
    private float _throttle;
    private float _elapsed;
    private bool _manual;

    public override void _Ready()
    {
        _throttle = Position.X;
    }

    public override void _Process(double deltaSeconds)
    {
        _elapsed += (float)deltaSeconds;
        Vector3 position = Position;
        if (_pendingStep != 0)
        {
            _manual = true;
            position.X = Math.Clamp(position.X + 0.2f * _pendingStep, -0.6f, 0.6f);
            _pendingStep = 0;
        }
        else if (!_manual)
        {
            position.X = 0.45f * MathF.Sin(_elapsed * 0.7f);
        }
        Position = position;
        _throttle = position.X;
    }

    public override void _EditorGui(EditorGui gui)
    {
        var percent = (int)MathF.Round((_throttle + 0.6f) / 1.2f * 100.0f);
        gui.Text(_manual ? "C# | Manual throttle (%): " : "C# | Auto throttle (%): ", percent);
        if (gui.Button("Auto throttle"))
        {
            _manual = false;
        }
        if (gui.Button("Slower"))
        {
            _pendingStep--;
        }
        if (gui.Button("Faster"))
        {
            _pendingStep++;
        }
    }
}
