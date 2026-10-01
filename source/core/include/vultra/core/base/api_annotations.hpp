#pragma once

#if defined(__clang__)
#define VULTRA_REFLECT [[clang::annotate("vultra.reflect")]]
#define VULTRA_PROPERTY(metadata) [[clang::annotate("vultra.property:" metadata)]]
#define VULTRA_BIND_UI [[clang::annotate("vultra.bind.ui")]]
#define VULTRA_BIND_SCENE [[clang::annotate("vultra.bind.scene")]]
#else
#define VULTRA_REFLECT
#define VULTRA_PROPERTY(metadata)
#define VULTRA_BIND_UI
#define VULTRA_BIND_SCENE
#endif
