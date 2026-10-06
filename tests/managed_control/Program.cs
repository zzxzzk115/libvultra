using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Vultra.Interop;
using VultraScript;
using Vultra.Scripting;

internal static unsafe class Program
{
    private static VultraSceneTranslation s_Throttle = new() { Y = -0.92f };
    private static bool s_ClickFaster;
    private static bool s_ClickAuto;
    private static bool s_CopiedModel;
    private static bool s_RemovedCopy;
    private static bool s_SelectedModel;
    private static bool s_CreatedGroup;
    private static bool s_CreatedMesh;
    private static bool s_ReparentedMesh;

    private static void Main()
    {
        var camera = new CameraSettings();
        Require(camera.VerticalFov == 1.04719755f && camera.NearPlane == 0.01f && camera.FarPlane == 100,
                "Generated managed camera defaults differ from the annotated C++ defaults");
        var material = new MaterialParameters { BaseColor = new(0.2f, 0.4f, 0.8f, 0.5f), CoatWeight = 0.6f };
        Require(MaterialParameters.FromAbi(material.ToAbi()) == material,
                "Generated safe material conversion lost grouped color or scalar fields");
        var light = new LightSettings();
        Require(light.Color == System.Numerics.Vector3.One && light.Intensity == 1 && light.Range == 10 &&
                LightSettings.FromAbi(light.ToAbi()) == light,
                "Generated light defaults/conversion differ from the native property model");
        Require(new EnvironmentSettings().Intensity == 1,
                "Generated environment default differs from the native property model");
        var scene = new VultraSceneApi
        {
            Version = VultraAbi.Version,
            StructSize = (uint)sizeof(VultraSceneApi),
            RootId = &RootId,
            ChildId = &ChildId,
            CopyMeshModel = &CopyMeshModel,
            CreateNode = &CreateNode,
            CreateMesh = &CreateMesh,
            ReparentNode = &ReparentNode,
            DuplicateMesh = &DuplicateMesh,
            RemoveNode = &RemoveNode,
            SetMeshModel = &SetMeshModel,
            NodeTranslation = &NodeTranslation,
            SetNodeTranslation = &SetNodeTranslation
        };
        var ui = new VultraUiApi
        {
            Version = VultraAbi.Version,
            StructSize = (uint)sizeof(VultraUiApi),
            Text = &Text,
            Button = &Button
        };
        var world = new ScriptScene(&scene);
        var script = new ThrottleController();
        script.Attach(world, 2);
        world.Begin(default);
        script._Ready();
        var copy = script.DuplicateMesh(world.Root);
        copy.CopyMeshModel(script);
        copy.SetMeshModel("8fc32ebe-cf5e-45b6-b93f-05c1ae8f165b");
        copy.Remove();
        var group = world.Root.CreateChild("Group");
        var mesh = world.Root.CreateMeshChild("Mesh", "8fc32ebe-cf5e-45b6-b93f-05c1ae8f165b");
        mesh.Reparent(group);
        Require(copy.Id == 3 && group.Id == 4 && mesh.Id == 5 && s_CopiedModel && s_SelectedModel &&
                s_RemovedCopy && s_CreatedGroup && s_CreatedMesh && s_ReparentedMesh,
                "Managed scene composition did not use the generated scene ABI");
        world.End();
        Process(script, world, 1.0f);
        var automatic = s_Throttle.X;
        Require(automatic > 0.2f, "Automatic throttle did not update the scene");

        s_ClickFaster = true;
        script._EditorGui(new EditorGui(&ui, default));
        Require(s_Throttle.X == automatic, "GUI callback modified the scene outside update");
        Process(script, world, 0.0f);
        var manual = s_Throttle.X;
        Require(Math.Abs(manual - Math.Clamp(automatic + 0.2f, -0.6f, 0.6f)) < 0.0001f,
                "Faster button did not apply on the next update");
        Process(script, world, 1.0f);
        Require(s_Throttle.X == manual, "Manual throttle did not override automatic motion");

        s_ClickAuto = true;
        script._EditorGui(new EditorGui(&ui, default));
        Process(script, world, 0.5f);
        Require(Math.Abs(s_Throttle.X - manual) > 0.01f, "Auto button did not resume throttle motion");

        for (var index = 0; index < 128; index++)
        {
            Process(script, world, 1.0f / 60.0f);
            script._EditorGui(new EditorGui(&ui, default));
        }
        var before = GC.GetAllocatedBytesForCurrentThread();
        for (var index = 0; index < 1024; index++)
        {
            Process(script, world, 1.0f / 60.0f);
        }
        var updateBytes = GC.GetAllocatedBytesForCurrentThread() - before;
        before = GC.GetAllocatedBytesForCurrentThread();
        for (var index = 0; index < 1024; index++)
        {
            script._EditorGui(new EditorGui(&ui, default));
        }
        var guiBytes = GC.GetAllocatedBytesForCurrentThread() - before;
        Require(updateBytes == 0 && guiBytes == 0, $"Managed callbacks allocated: process {updateBytes}, UI {guiBytes}");
        script._ExitTree();
        bool expired = false;
        try
        {
            _ = script.Position;
        }
        catch (InvalidOperationException)
        {
            expired = true;
        }
        Require(expired, "Scene node remained accessible outside the process callback");
    }

    private static void Process(ThrottleController script, ScriptScene world, float deltaSeconds)
    {
        world.Begin(default);
        try
        {
            script._Process(deltaSeconds);
        }
        finally
        {
            world.End();
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus RootId(VultraSceneFrame frame, ulong* value)
    {
        *value = 1;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus ChildId(VultraSceneFrame frame, ulong parent, ulong index, ulong* value)
    {
        if (parent != 1 || index != 2)
        {
            return VultraStatus.InvalidArgument;
        }
        *value = 2;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus CreateNode(VultraSceneFrame frame, ulong parent, byte* data, ulong size, ulong* value)
    {
        if (parent != 1 || Encoding.UTF8.GetString(new ReadOnlySpan<byte>(data, checked((int)size))) != "Group")
        {
            return VultraStatus.InvalidArgument;
        }
        *value = 4;
        s_CreatedGroup = true;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus CreateMesh(VultraSceneFrame frame, ulong parent, byte* name, ulong nameSize,
                                           byte* assetId, ulong assetSize, ulong* value)
    {
        if (parent != 1 || Encoding.UTF8.GetString(new ReadOnlySpan<byte>(name, checked((int)nameSize))) != "Mesh" ||
            Encoding.UTF8.GetString(new ReadOnlySpan<byte>(assetId, checked((int)assetSize))) !=
                "8fc32ebe-cf5e-45b6-b93f-05c1ae8f165b")
        {
            return VultraStatus.InvalidArgument;
        }
        *value = 5;
        s_CreatedMesh = true;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus ReparentNode(VultraSceneFrame frame, ulong node, ulong newParent)
    {
        if (node != 5 || newParent != 4)
        {
            return VultraStatus.InvalidArgument;
        }
        s_ReparentedMesh = true;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus DuplicateMesh(VultraSceneFrame frame, ulong source, ulong parent, ulong* value)
    {
        if (source != 2 || parent != 1)
        {
            return VultraStatus.InvalidArgument;
        }
        *value = 3;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus CopyMeshModel(VultraSceneFrame frame, ulong target, ulong source)
    {
        if (target != 3 || source != 2)
        {
            return VultraStatus.InvalidArgument;
        }
        s_CopiedModel = true;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus SetMeshModel(VultraSceneFrame frame, ulong node, byte* data, ulong size)
    {
        if (node != 3 || Encoding.UTF8.GetString(new ReadOnlySpan<byte>(data, checked((int)size))) !=
            "8fc32ebe-cf5e-45b6-b93f-05c1ae8f165b")
        {
            return VultraStatus.InvalidArgument;
        }
        s_SelectedModel = true;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus RemoveNode(VultraSceneFrame frame, ulong node)
    {
        if (node != 3)
        {
            return VultraStatus.InvalidArgument;
        }
        s_RemovedCopy = true;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus NodeTranslation(VultraSceneFrame frame, ulong node, VultraSceneTranslation* value)
    {
        if (node != 2)
        {
            return VultraStatus.InvalidArgument;
        }
        *value = s_Throttle;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus SetNodeTranslation(VultraSceneFrame frame, ulong node, VultraSceneTranslation value)
    {
        if (node != 2)
        {
            return VultraStatus.InvalidArgument;
        }
        s_Throttle = value;
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus Text(VultraUiFrame frame, byte* data, ulong size) => VultraStatus.Ok;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus Button(VultraUiFrame frame, byte* data, ulong size, byte* clicked)
    {
        var label = new ReadOnlySpan<byte>(data, checked((int)size));
        *clicked = 0;
        if (label.SequenceEqual("Faster"u8) && s_ClickFaster)
        {
            s_ClickFaster = false;
            *clicked = 1;
        }
        if (label.SequenceEqual("Auto throttle"u8) && s_ClickAuto)
        {
            s_ClickAuto = false;
            *clicked = 1;
        }
        return VultraStatus.Ok;
    }
}
