#pragma once

#if defined(__clang__)
#define VULTRA_REFLECT [[clang::annotate("vultra.reflect")]]
#define VULTRA_PROPERTY(metadata) [[clang::annotate("vultra.property:" metadata)]]
#define VULTRA_BIND_UI [[clang::annotate("vultra.bind.ui")]]
#define VULTRA_BIND_SCENE [[clang::annotate("vultra.bind.scene")]]
#define VULTRA_BIND_EXPERIMENT [[clang::annotate("vultra.bind.experiment")]]
#define VULTRA_BIND_POD [[clang::annotate("vultra.bind.pod")]]
#else
#define VULTRA_REFLECT
#define VULTRA_PROPERTY(metadata)
#define VULTRA_BIND_UI
#define VULTRA_BIND_SCENE
#define VULTRA_BIND_EXPERIMENT
#define VULTRA_BIND_POD
#endif
