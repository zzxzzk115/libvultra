# FileWatch integration

Upstream: [ThomasMonkman/FileWatch](https://github.com/ThomasMonkman/filewatch), commit [`a59891baf375b73ff28144973a6fafd3fe40aa21`](https://github.com/ThomasMonkman/filewatch/tree/a59891baf375b73ff28144973a6fafd3fe40aa21). License: [MIT](LICENSE).

Local patch: Linux watches include `IN_MOVED_FROM` and `IN_MOVED_TO`, mapped to the existing rename events. This detects editor saves that atomically rename a replacement over a shader. The Linux read loop uses a bounded `poll` wait so destroying a watcher after its directory was deleted cannot block forever on an already-removed inotify watch. The deleted move constructor uses the injected class name required by C++20 and later. The upstream Windows/macOS watch paths and copyright notice are unchanged.

Linux inotify watches are not recursive. `ShaderPipeline::Watch` owns one FileWatch per directory in the shader tree and reconciles the set after a debounced event, including newly created/deleted subdirectories. Existing watches are retained. The Windows backend retains its single recursive watch. Watch destruction joins callbacks before their captured state is destroyed.
