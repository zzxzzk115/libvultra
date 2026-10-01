using System;
using System.Buffers;
using System.Buffers.Text;
using System.Text;
using Vultra.Interop;

namespace Vultra.Scripting;

// This borrowed value is created only in the engine's UI phase.
public readonly ref struct EditorGui
{
    private readonly nint _api;
    private readonly VultraUiFrame _frame;

    internal unsafe EditorGui(VultraUiApi* api, VultraUiFrame frame)
    {
        _api = (nint)api;
        _frame = frame;
    }

    public void Text(ReadOnlySpan<char> text)
    {
        WithUtf8(text, false);
    }

    public bool Button(ReadOnlySpan<char> label)
    {
        return WithUtf8(label, true);
    }

    public void Text(ReadOnlySpan<char> label, int value)
    {
        int capacity = checked(Encoding.UTF8.GetMaxByteCount(label.Length) + 11);
        byte[]? rented = null;
        Span<byte> buffer = capacity <= 256 ? stackalloc byte[capacity] :
            (rented = ArrayPool<byte>.Shared.Rent(capacity));
        try
        {
            int length = Encoding.UTF8.GetBytes(label, buffer);
            if (!Utf8Formatter.TryFormat(value, buffer[length..], out int digits))
            {
                throw new InvalidOperationException("GUI integer label exceeds its buffer");
            }
            Text(buffer[..(length + digits)]);
        }
        finally
        {
            if (rented != null)
            {
                ArrayPool<byte>.Shared.Return(rented);
            }
        }
    }

    private bool WithUtf8(ReadOnlySpan<char> text, bool button)
    {
        int capacity = Encoding.UTF8.GetMaxByteCount(text.Length);
        byte[]? rented = null;
        Span<byte> buffer = capacity <= 256 ? stackalloc byte[capacity] :
            (rented = ArrayPool<byte>.Shared.Rent(capacity));
        try
        {
            int length = Encoding.UTF8.GetBytes(text, buffer);
            if (button)
            {
                return Button(buffer[..length]);
            }
            Text(buffer[..length]);
            return false;
        }
        finally
        {
            if (rented != null)
            {
                ArrayPool<byte>.Shared.Return(rented);
            }
        }
    }

    public void Text(ReadOnlySpan<byte> utf8)
    {
        TextUtf8(utf8);
    }

    public bool Button(ReadOnlySpan<byte> utf8)
    {
        return ButtonUtf8(utf8);
    }

    private unsafe void TextUtf8(ReadOnlySpan<byte> utf8)
    {
        fixed (byte* data = utf8)
        {
            Check(((VultraUiApi*)_api)->Text(_frame, data, (ulong)utf8.Length));
        }
    }

    private unsafe bool ButtonUtf8(ReadOnlySpan<byte> utf8)
    {
        byte clicked;
        fixed (byte* data = utf8)
        {
            Check(((VultraUiApi*)_api)->Button(_frame, data, (ulong)utf8.Length, &clicked));
        }
        return clicked != 0;
    }

    private static void Check(VultraStatus status)
    {
        if (status != VultraStatus.Ok)
        {
            throw new InvalidOperationException($"Vultra UI API status {status}");
        }
    }
}
