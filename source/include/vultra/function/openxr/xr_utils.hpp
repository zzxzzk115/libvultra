// Copyright 2023, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

// OpenXR Tutorial for Khronos Group

#pragma once

// C/C++ Headers
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Debugbreak
// #if defined(_MSC_VER) || defined(__MINGW32__)
// #define DEBUG_BREAK() __debugbreak()
// #else
// #include <signal.h>
// #define DEBUG_BREAK() raise(SIGTRAP)
// #endif

inline bool IsStringInVector(const std::vector<const char*>& list, const char* name)
{
    return std::find(list.begin(), list.end(), name) != list.end();
}

template<typename T>
inline bool BitwiseCheck(const T& value, const T& checkValue)
{
    return ((value & checkValue) == checkValue);
}

template<typename T>
T Align(T value, T alignment)
{
    return (value + (alignment - 1)) & ~(alignment - 1);
}

// NOLINTBEGIN
inline std::string GetEnv(const std::string& variable)
{
    const char* value = std::getenv(variable.c_str());
    return value != nullptr ? std::string(value) : std::string("");
}
// NOLINTEND

inline void SetEnv(const std::string& variable, const std::string& value)
{
#if defined(_MSC_VER) || defined(__MINGW32__)
    _putenv_s(variable.c_str(), value.c_str());
#else
    setenv(variable.c_str(), value.c_str(), 1);
#endif
}

inline std::string ReadTextFile(const std::string& filepath)
{
    std::ifstream stream(filepath);
    std::string   output;
    if (!stream.is_open())
    {
        std::cerr << "Could not read file " << filepath << ". File does not exist." << std::endl;
        return "";
    }
    std::string line;
    while (std::getline(stream, line))
    {
        output.append(line + "\n");
    }
    return output;
}

inline std::vector<char> ReadBinaryFile(const std::string& filepath)
{
    std::ifstream stream(filepath, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
    {
        std::cerr << "Could not read file " << filepath << ". File does not exist." << std::endl;
        return {};
    }
    std::streamoff    size = stream.tellg();
    std::vector<char> output(static_cast<size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(output.data(), size);
    return output;
}

#if defined(__ANDROID__)
#include <android/asset_manager.h>
inline std::string ReadTextFile(const std::string& filepath, AAssetManager* assetManager)
{
    AAsset*     file       = AAssetManager_open(assetManager, filepath.c_str(), AASSET_MODE_BUFFER);
    size_t      fileLength = AAsset_getLength(file);
    std::string text(fileLength, '\0');
    AAsset_read(file, (void*)text.data(), fileLength);
    AAsset_close(file);
    return text;
}

inline std::vector<char> ReadBinaryFile(const std::string& filepath, AAssetManager* assetManager)
{
    AAsset*           file       = AAssetManager_open(assetManager, filepath.c_str(), AASSET_MODE_BUFFER);
    size_t            fileLength = AAsset_getLength(file);
    std::vector<char> binary(fileLength);
    AAsset_read(file, (void*)binary.data(), fileLength);
    AAsset_close(file);
    return binary;
}
#endif

#if defined(_MSC_VER) || defined(__MINGW32__)
#define strncpy(dst, src, count) strcpy_s(dst, count, src)
#endif
