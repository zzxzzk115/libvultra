# FileWatch integration

Upstream: [ThomasMonkman/FileWatch](https://github.com/ThomasMonkman/filewatch), commit [`a59891baf375b73ff28144973a6fafd3fe40aa21`](https://github.com/ThomasMonkman/filewatch/tree/a59891baf375b73ff28144973a6fafd3fe40aa21). License: [MIT](LICENSE).

Local patches retain the upstream copyright and MIT notice. Linux watches include `IN_MOVED_FROM` and
`IN_MOVED_TO`, mapped to the existing rename events, so atomic editor saves invalidate shaders. Its bounded `poll`
wait also permits destruction after a watched directory disappears. The deleted move constructor uses the
injected class name required by C++20 and later.
Windows watches omit last-access notifications: reading a shader must not invalidate it on NTFS.
Content, size and rename notifications remain enabled.

Linux inotify watches are not recursive. `ShaderPipeline::Watch` owns one FileWatch per directory in the shader tree and reconciles the set after a debounced event, including newly created/deleted subdirectories. Existing watches are retained. The Windows backend retains its single recursive watch. Watch destruction joins callbacks before their captured state is destroyed.

The macOS patch handles coalesced atomic-save events whose temporary filename was never captured in the directory
snapshot. A missing rename entry triggers reconciliation instead of an end-iterator dereference. Snapshot creation
ignores files that disappear before open/stat and transfers its single file descriptor into FileState, fixing the
previous extra-open leak. The actual shader-material test covers 512 atomic replacements with transient files,
event delivery and descriptor retirement, followed by compile/build rejection and live-material recovery.

The C++23 `createExpected` API reports invalid paths, native setup/start failures and Expected callback rejection.
`statusExpected` retains worker errors after startup. Legacy constructors and void callbacks adapt to the same
implementation; their exception boundaries remain transitional. Native readers are shared, and the Expected
worker path does not use exception transport. Thread creation uses returned pthread errors on POSIX, or
[`_beginthreadex`](https://learn.microsoft.com/cpp/c-runtime-library/reference/beginthread-beginthreadex) on Windows.
Partial startup joins any created worker before closing its native resources. The owning thread destroys watches;
callbacks must not destroy their own watch. Internal join ownership is an invariant, not a recoverable input error.

macOS uses UTF-8 stream paths, checks stream startup, retains its run loop through worker retirement and bounds
the run-loop wait to cover immediate stop after startup. It still uses upstream run-loop scheduling, which emits
the existing macOS 13 deprecation warning; the warning is not suppressed. Windows setup/overlapped-read errors
return diagnostics, pending I/O retires before its buffers die, and both event handles are closed. Linux reports
`IN_IGNORED` as a stopped watch so an owner can replace it after recreating the directory. Windows changes require
native build/runtime qualification; an Android inotify test does not establish desktop Linux rendering coverage.

The standalone `test-file-watch-expected` target isolates pthread creation faults from other template instances.
It checks both first- and second-thread failure, descriptor retention and subsequent event delivery. The material
test exercises Expected callback failure, Unicode single-file watching and 512 atomic replacements with real GPU
compile/build recovery. The fixed upstream source and cumulative local patch are retained with validation evidence.
