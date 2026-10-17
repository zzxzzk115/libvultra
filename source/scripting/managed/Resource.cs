using System.Text.Json;

namespace Vultra.Scripting;

// A borrowed world resource. Native identity checks remain active after removal or world shutdown.
public class Resource
{
    internal ScriptScene Scene { get; }
    internal ulong Id { get; }

    internal Resource(ScriptScene scene, ulong id)
    {
        Scene = scene;
        Id = id;
    }

    public string TypeName => Scene.ObjectType(Id);
    public T? GetProperty<T>(string name) => JsonSerializer.Deserialize<T>(Scene.ReadPropertyJson(Id, name));
    public void SetProperty<T>(string name, T value) => Scene.WritePropertyJson(Id, name, JsonSerializer.Serialize(value));
    public void Remove() => Scene.RemoveResource(Id);
}
