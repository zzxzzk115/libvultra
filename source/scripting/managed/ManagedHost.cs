using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
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
    public void* Previous;
    public void* Instance;
}

internal sealed class ScriptLoadContext(string path) : AssemblyLoadContext(isCollectible: true)
{
    private readonly AssemblyDependencyResolver _resolver = new(path);

    protected override Assembly? Load(AssemblyName name)
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
    private sealed class State
    {
        public ScriptLoadContext? Context;
        public Node? Script;
        public ScriptScene? Scene;
        public VultraUiApi* Ui;
        public bool Ready;
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

    private static Dictionary<string, string> SaveFields(Node script)
    {
        var values = new Dictionary<string, string>();
        // A process-wide options cache would retain types from collectible script assemblies.
        var options = new JsonSerializerOptions { IncludeFields = true };
        foreach (var field in ReloadFields(script.GetType()))
        {
            try
            {
                values.Add(FieldKey(field), JsonSerializer.Serialize(field.GetValue(script), field.FieldType, options));
            }
            catch (Exception error)
            {
                Console.Error.WriteLine($"Save C# reload field {FieldKey(field)}: {error.Message}");
            }
        }
        return values;
    }

    private static void RestoreFields(Node script, Dictionary<string, string> values)
    {
        var options = new JsonSerializerOptions { IncludeFields = true };
        foreach (var field in ReloadFields(script.GetType()))
        {
            if (!values.TryGetValue(FieldKey(field), out var json))
            {
                continue;
            }
            try
            {
                field.SetValue(script, JsonSerializer.Deserialize(json, field.FieldType, options));
            }
            catch (Exception error)
            {
                Console.Error.WriteLine($"Restore C# reload field {FieldKey(field)}: {error.Message}");
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Initialize(void* hostData, void* pluginData)
    {
        var host = (VultraHostApi*)hostData;
        var plugin = (VultraPluginApi*)pluginData;
        if (host == null || plugin == null || plugin->UserData == null ||
            host->Version != VultraAbi.Version || host->StructSize < sizeof(VultraHostApi) ||
            host->Ui == null || host->Ui->Version != VultraAbi.Version ||
            host->Ui->StructSize < sizeof(VultraUiApi) ||
            host->Scene == null || host->Scene->Version != VultraAbi.Version ||
            host->Scene->StructSize < sizeof(VultraSceneApi) ||
            plugin->Version != VultraAbi.Version || plugin->StructSize < sizeof(VultraPluginApi))
        {
            return (int)VultraStatus.InvalidArgument;
        }
        var request = (ManagedLoadRequest*)plugin->UserData;
        if (request->Path == null || request->PathSize == 0 || request->PathSize > int.MaxValue || request->TypeName == null ||
            request->TypeNameSize == 0 || request->TypeNameSize > int.MaxValue || request->NodeId == 0)
        {
            return (int)VultraStatus.InvalidArgument;
        }
        ScriptLoadContext? context = null;
        try
        {
            var path = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(request->Path, (int)request->PathSize));
            var typeName = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(request->TypeName, (int)request->TypeNameSize));
            context = new ScriptLoadContext(path);
            using var source = new MemoryStream(File.ReadAllBytes(path));
            var assembly = context.LoadFromStream(source);
            var type = assembly.GetType(typeName, throwOnError: true)!;
            if (!typeof(Node).IsAssignableFrom(type) || type.IsAbstract)
            {
                throw new InvalidOperationException($"{typeName} must derive from Node");
            }
            var scene = new ScriptScene(host->Scene);
            var script = (Node)Activator.CreateInstance(type)!;
            script.Attach(scene, request->NodeId);
            if (request->Previous != null)
            {
                var old = (State)GCHandle.FromIntPtr((nint)request->Previous).Target!;
                RestoreFields(script, SaveFields(old.Script!));
            }
            var state = new State { Context = context, Script = script, Scene = scene, Ui = host->Ui };
            plugin->UserData = (void*)GCHandle.ToIntPtr(GCHandle.Alloc(state));
            plugin->Update = &Update;
            plugin->OnGui = &OnGui;
            plugin->Stop = &Stop;
            request->Instance = plugin->UserData;
            return (int)VultraStatus.Ok;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Load C# script: {error}");
            context?.Unload();
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
                if (!state.Ready)
                {
                    state.Script!._Ready();
                    state.Ready = true;
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
    private static VultraStatus Stop(void* userData)
    {
        var handle = GCHandle.FromIntPtr((nint)userData);
        var state = (State)handle.Target!;
        var status = VultraStatus.Ok;
        try
        {
            if (state.Ready)
            {
                state.Script?._ExitTree();
            }
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"C# script stop: {error}");
            status = VultraStatus.Error;
        }
        finally
        {
            state.Scene?.End();
            state.Script = null;
            state.Scene = null;
            try
            {
                state.Context?.Unload();
            }
            catch (Exception error)
            {
                Console.Error.WriteLine($"Unload C# script: {error}");
                status = VultraStatus.Error;
            }
            finally
            {
                state.Context = null;
                handle.Free();
            }
        }
        return status;
    }
}
