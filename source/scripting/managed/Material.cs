namespace Vultra.Scripting;

public sealed class Material : Resource
{
    internal Material(ScriptScene scene, ulong id) : base(scene, id)
    {
    }

    public string Name
    {
        get => Scene.MaterialName(Id);
        set => SetProperty("name", value);
    }
    public MaterialParameters Parameters
    {
        get => Scene.MaterialParameters(Id);
        set => Scene.SetMaterialParameters(Id, value);
    }
}
