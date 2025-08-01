#pragma once

#include <openxr/openxr.h>

XrBool32 OpenXRMessageCallbackFunction(XrDebugUtilsMessageSeverityFlagsEXT         messageSeverity,
                                       XrDebugUtilsMessageTypeFlagsEXT             messageType,
                                       const XrDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                       void*                                       pUserData);

// NOLINTBEGIN
XrDebugUtilsMessengerEXT CreateOpenXRDebugUtilsMessenger(XrInstance m_XrInstance);
void DestroyOpenXRDebugUtilsMessenger(XrInstance m_XrInstance, XrDebugUtilsMessengerEXT debugUtilsMessenger);
// NOLINTEND