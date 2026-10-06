namespace Vultra.Scripting;

public sealed class Material
{
    internal ScriptScene Scene { get; }
    internal ulong Id { get; }

    internal Material(ScriptScene scene, ulong id)
    {
        Scene = scene;
        Id = id;
    }

    public string Name => Scene.MaterialName(Id);
    public MaterialParameters Parameters
    {
        get => Scene.MaterialParameters(Id);
        set => Scene.SetMaterialParameters(Id, value);
    }
    public void Remove() => Scene.RemoveMaterial(Id);
}
