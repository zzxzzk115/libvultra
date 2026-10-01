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
    public Vector3 Position
    {
        get => Scene.Position(Id);
        set => Scene.SetPosition(Id, value);
    }
}
