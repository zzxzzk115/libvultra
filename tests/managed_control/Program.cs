using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Vultra.Interop;
using VultraScript;
using Vultra.Scripting;

internal static unsafe class Program
{
    private static VultraSceneTranslation s_Throttle = new() { Y = -0.92f };
    private static bool s_ClickFaster;
    private static bool s_ClickAuto;

    private static void Main()
    {
        var scene = new VultraSceneApi
        {
            Version = VultraAbi.Version,
            StructSize = (uint)sizeof(VultraSceneApi),
            RootId = &RootId,
            ChildId = &ChildId,
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
