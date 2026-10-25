#pragma once

// Tool-side opt-in; ordinary builds consume checked-in output without a reflection runtime.
#if defined(__clang__)
#define VULTRA_REFLECT [[clang::annotate("vultra.reflect")]]
#define VULTRA_REFERENCE(category) [[clang::annotate("vultra.reference:" category)]]
#define VULTRA_REFLECT_NODE [[clang::annotate("vultra.reflect.node")]]
#define VULTRA_REFLECT_RESOURCE [[clang::annotate("vultra.reflect.resource")]]
#define VULTRA_REFLECT_COMPONENT [[clang::annotate("vultra.reflect.component")]]
#define VULTRA_PROPERTY(metadata) [[clang::annotate("vultra.property:" metadata)]]
#define VULTRA_BIND_UI [[clang::annotate("vultra.bind.ui")]]
#define VULTRA_BIND_SCENE [[clang::annotate("vultra.bind.scene")]]
#define VULTRA_BIND_EXPERIMENT [[clang::annotate("vultra.bind.experiment")]]
#define VULTRA_BIND_COMPUTE [[clang::annotate("vultra.bind.compute")]]
#define VULTRA_BIND_POD [[clang::annotate("vultra.bind.pod")]]
#define VULTRA_BIND_ENUM [[clang::annotate("vultra.bind.enum")]]
#define VULTRA_ABI_TABLE [[clang::annotate("vultra.abi_table")]]
#else
#define VULTRA_REFLECT
#define VULTRA_REFERENCE(category)
#define VULTRA_REFLECT_NODE
#define VULTRA_REFLECT_RESOURCE
#define VULTRA_REFLECT_COMPONENT
#define VULTRA_PROPERTY(metadata)
#define VULTRA_BIND_UI
#define VULTRA_BIND_SCENE
#define VULTRA_BIND_EXPERIMENT
#define VULTRA_BIND_COMPUTE
#define VULTRA_BIND_POD
#define VULTRA_BIND_ENUM
#define VULTRA_ABI_TABLE
#endif
