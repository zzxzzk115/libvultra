#pragma once

namespace vultra
{
    enum class FbxMaterialConvention
    {
        ePhong,
        eOrcaMetallicRoughness
    };

    struct FbxImportOptions
    {
        FbxMaterialConvention materialConvention = FbxMaterialConvention::ePhong;
        bool                  directXNormalMaps  = false;
        friend bool           operator==(const FbxImportOptions&, const FbxImportOptions&) = default;
    };
} // namespace vultra
