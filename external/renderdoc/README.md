# RenderDoc in-application API

`renderdoc_app.h` is the unmodified MIT-licensed RenderDoc in-application API header, obtained from the public [RenderDoc repository](https://github.com/baldurk/renderdoc/blob/v1.x/renderdoc/api/app/renderdoc_app.h). It was copied from the Arch Linux `renderdoc 1.45-1.1` package, built from upstream commit `2fc0bc04cb95499635f63986a55bc6f67849dd9f`, and exposes API version 1.7.0 and has SHA-256 `b7005e7dc34c3635046868bbd76d81b9b055aede0f56daa0bd39fedee0639ffb`. There are no local patches.

Vultra uses it only to call an already injected RenderDoc instance. It does not link to or load RenderDoc as a required runtime dependency.
