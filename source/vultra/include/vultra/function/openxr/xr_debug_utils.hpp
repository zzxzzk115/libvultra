#pragma once

#include <openxr/openxr.h>

XRAPI_ATTR XrBool32 XRAPI_CALL OpenXRMessageCallbackFunction(XrDebugUtilsMessageSeverityFlagsEXT         messageSeverity,
                                                             XrDebugUtilsMessageTypeFlagsEXT             messageType,
                                                             const XrDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                             void*                                       pUserData);

// NOLINTBEGIN
XrDebugUtilsMessengerEXT CreateOpenXRDebugUtilsMessenger(XrInstance m_XrInstance);
void DestroyOpenXRDebugUtilsMessenger(XrInstance m_XrInstance, XrDebugUtilsMessengerEXT debugUtilsMessenger);
// NOLINTEND
