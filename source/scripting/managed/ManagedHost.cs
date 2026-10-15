using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Vultra.Interop;
using Vultra.Scripting;

namespace Vultra.ManagedHost;

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ManagedLoadRequest
{
    public byte* Path;
    public ulong PathSize;
    public byte* TypeName;
    public ulong TypeNameSize;
    public ulong NodeId;
    public byte* NodeType;
    public ulong NodeTypeSize;
    public void* Peer;
    public void* Previous;
    public void* Instance;
}

internal sealed class ScriptLoadContext(string path) :
    AssemblyLoadContext
(isCollectible: true)
{
    private readonly AssemblyDependencyResolver _resolver = new(path);

    protected override Assembly ? Load(AssemblyName name)
    {
        // Share engine types with the default context so user scripts derive from the same Node.
        if (name.Name == typeof(Node).Assembly.GetName().Name)
        {
            return typeof(Node).Assembly;
        }
        var resolved = _resolver.ResolveAssemblyToPath(name);
        return resolved == null ? null : LoadFromAssemblyPath(resolved);
    }
}

public static unsafe class Entry
{
    private sealed class Module
    {
        public required ScriptLoadContext Context;
        public required Assembly          Assembly;
        public required                   byte[] Hash;
        public int                        Users;
    }

    private sealed class World(ScriptScene scene)
    {
        public readonly ScriptScene Scene = scene;
        public readonly Dictionary<string, Module> Modules =
            new(OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal);
    }

    private sealed class State
    {
        public World? World;
        public Module? Module;
        public string Path = "";
        public Node? Script;
        public ScriptScene? Scene;
        public VultraUiApi* Ui;
        public bool         Active;
    }

    private static IEnumerable<FieldInfo> ReloadFields(Type type)
    {
        for (Type? current = type; current != null && current != typeof(Node); current = current.BaseType)
        {
            foreach (var field in current.GetFields(BindingFlags.Instance | BindingFlags.Public |
                                                    BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (!field.IsInitOnly && !field.IsDefined(typeof(NonSerializedAttribute), false))
                {
                    yield return field;
                }
            }
        }
    }

    private static string FieldKey(FieldInfo field) => $"{field.DeclaringType!.FullName}.{field.Name}";

    private readonly record struct ReloadValue(string? Json, object? Reference);

    private static Dictionary<string, ReloadValue> SaveFields(Node script)
    {
        var values = new Dictionary<string, ReloadValue>();
        // Local options cannot retain types from unloaded script assemblies in a global cache.
        var options = new JsonSerializerOptions { IncludeFields = true };
        foreach (var field in ReloadFields(script.GetType()))
        {
            try
            {
                var value = field.GetValue(script);
                if (value is Node or Material)
                {
                    if (value.GetType().Assembly != typeof(Node).Assembly)
                    {
                        throw new NotSupportedException("References to script instances need common reference metadata");
                    }
                    // Native wrappers belong to the shared scene and survive a code replacement.
                    values.Add(FieldKey(field), new ReloadValue(null, value));
                }
                else
                {
                    values.Add(FieldKey(field), new ReloadValue(
                        JsonSerializer.Serialize(value, field.FieldType, options), null));
                }
            }
            catch (Exception error)
            {
                throw new InvalidOperationException($"Save reload field {FieldKey(field)}", error);
            }
        }
        return values;
    }

    private static void RestoreFields(Node script, Dictionary<string, ReloadValue> values)
    {
        var options = new JsonSerializerOptions { IncludeFields = true };
        foreach (var field in ReloadFields(script.GetType()))
        {
            if (!values.TryGetValue(FieldKey(field), out var saved))
            {
                continue;
            }
            try
            {
                if (saved.Reference != null && !field.FieldType.IsInstanceOfType(saved.Reference))
                {
                    throw new InvalidCastException("Reload changed the native reference type");
                }
                var value = saved.Reference ?? JsonSerializer.Deserialize(saved.Json!, field.FieldType, options);
                field.SetValue(script, value);
            }
            catch (Exception error)
            {
                throw new InvalidOperationException($"Restore reload field {FieldKey(field)}", error);
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

    public static int Initialize(void* hostData, void* pluginData)
    {
        var host   = (VultraHostApi*)hostData;
        var plugin = (VultraPluginApi*)pluginData;
        if (host == null || plugin == null || plugin->UserData == null || host->Version != VultraAbi.Version ||
            host->StructSize != sizeof(VultraHostApi) ||
            (host->Ui != null && (host->Ui->Version != VultraAbi.Version || host->Ui->StructSize != sizeof(VultraUiApi))) ||
            host->Scene == null ||
            host->Scene->Version != VultraAbi.Version || host->Scene->StructSize != sizeof(VultraSceneApi) ||
            plugin->Version != VultraAbi.Version || plugin->StructSize != sizeof(VultraPluginApi))
        {
            return (int)VultraStatus.InvalidArgument;
        }
        var request = (ManagedLoadRequest*)plugin->UserData;
        if (request->Path == null || request->PathSize == 0 || request->PathSize > int.MaxValue ||
            request->TypeName == null || request->TypeNameSize == 0 || request->TypeNameSize > int.MaxValue ||
            request->NodeId == 0 || request->NodeType == null || request->NodeTypeSize == 0 ||
            request->NodeTypeSize > int.MaxValue)
        {
            return (int)VultraStatus.InvalidArgument;
        }
        ScriptLoadContext? stagedContext = null;
        try
        {
            var path = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(request->Path, (int)request->PathSize));
            var typeName =
                Encoding.UTF8.GetString(new ReadOnlySpan<byte>(request->TypeName, (int)request->TypeNameSize));
            var shared = request->Previous != null ? request->Previous : request->Peer;
            var world  = shared == null ? new World(new ScriptScene(host->Scene)) :
                                          ((State)GCHandle.FromIntPtr((nint)shared).Target!).World!;
            path       = Path.GetFullPath(path);
            var image  = File.ReadAllBytes(path);
            var hash   = SHA256.HashData(image);
            if (!world.Modules.TryGetValue(path, out var module) || !module.Hash.AsSpan().SequenceEqual(hash))
            {
                stagedContext    = new ScriptLoadContext(path);
                using var source = new MemoryStream(image);
                module           = new Module { Context  = stagedContext,
                                                Assembly = stagedContext.LoadFromStream(source),
                                                Hash     = hash };
            }
            var type = module.Assembly.GetType(typeName, throwOnError: true)!;
            if (!typeof(Node).IsAssignableFrom(type) || type.IsAbstract)
            {
                throw new InvalidOperationException($"{typeName} must derive from Node");
            }
            var scene  = world.Scene;
            var script = (Node)Activator.CreateInstance(type)!;
            ScriptScene.ValidateScriptBase(script,
                                           new ReadOnlySpan<byte>(request->NodeType, (int)request->NodeTypeSize));
            script.Attach(scene, request->NodeId);
            var previous = request->Previous == null ? null :
                (State)GCHandle.FromIntPtr((nint)request->Previous).Target!;
            if (previous != null)
            {
                RestoreFields(script, SaveFields(previous.Script!));
            }
            var state         = new State { World  = world,
                                            Module = module,
                                            Path   = path,
                                            Script = script,
                                            Scene  = scene,
                                            Ui     = host->Ui };
            plugin->UserData  = (void*)GCHandle.ToIntPtr(GCHandle.Alloc(state));
            plugin->Update    = &Update;
            plugin->OnGui     = &OnGui;
            plugin->Stop      = &Stop;
            plugin->OnScene   = &OnScene;
            request->Instance = plugin->UserData;
            scene.RegisterScript(script);
            world.Modules[path] = module;
            module.Users++;
            stagedContext = null;
            return (int)VultraStatus.Ok;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Load C# script: {error}");
            stagedContext?.Unload();
            return (int)VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

    private static VultraStatus Update(void* userData, VultraSceneFrame frame, float deltaSeconds)
    {
        try
        {
            var state = (State)GCHandle.FromIntPtr((nint)userData).Target!;
            state.Scene!.Begin(frame);
            try
            {
                if (!state.Active)
                {
                    state.Scene.ActivateScript(state.Script!);
                    state.Active = true;
                }
                state.Script!._Process(deltaSeconds);
            }
            finally
            {
                state.Scene.End();
            }
            return VultraStatus.Ok;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"C# script update: {error}");
            return VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

    private static VultraStatus OnGui(void* userData, VultraUiFrame frame)
    {
        try
        {
            var state = (State)GCHandle.FromIntPtr((nint)userData).Target!;
            if (state.Ui == null)
            {
                return VultraStatus.InvalidArgument;
            }
            state.Script!._EditorGui(new EditorGui(state.Ui, frame));
            return VultraStatus.Ok;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"C# script UI: {error}");
            return VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus OnScene(void* userData, VultraSceneFrame frame, uint sceneEvent, float deltaSeconds)
    {
        try
        {
            var state = (State)GCHandle.FromIntPtr((nint)userData).Target!;
            state.Scene!.Begin(frame);
            try
            {
                if (!state.Active)
                {
                    state.Scene.ActivateScript(state.Script!);
                    state.Active = true;
                }
                switch (sceneEvent)
                {
                    case 0:
                        state.Script!._EnterTree();
                        break;
                    case 1:
                        state.Script!._Ready();
                        break;
                    case 2:
                        state.Script!._PhysicsProcess(deltaSeconds);
                        break;
                    case 3:
                        state.Script!._ExitTree();
                        break;
                    case 4:
                        break;
                    default:
                        return VultraStatus.InvalidArgument;
                }
            }
            finally
            {
                state.Scene.End();
            }
            return VultraStatus.Ok;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"C# script scene callback: {error}");
            return VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

    private static VultraStatus Stop(void* userData)
    {
        var handle = GCHandle.FromIntPtr((nint)userData);
        var state  = (State)handle.Target!;
        var status = VultraStatus.Ok;
        try
        {
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"C# script stop: {error}");
            status = VultraStatus.Error;
        }
        finally
        {
            state.Scene?.End();
            if (state.Script != null)
            {
                state.Scene?.RetireScript(state.Script);
            }
            state.Script = null;
            state.Scene  = null;
            try
            {
                var module = state.Module!;
                if (--module.Users == 0)
                {
                    if (state.World!.Modules.TryGetValue(state.Path, out var current) &&
                        ReferenceEquals(current, module))
                    {
                        state.World.Modules.Remove(state.Path);
                    }
                    module.Context.Unload();
                }
            }
            catch (Exception error)
            {
                Console.Error.WriteLine($"Unload C# module: {error}");
                status = VultraStatus.Error;
            }
            finally
            {
                state.Module = null;
                state.World  = null;
                handle.Free();
            }
        }
        return status;
    }
}
