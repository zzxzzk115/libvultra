import type_conformance_types;

public struct AddModel : IMaterialModel, IMarker
{
    uint4 value;

    uint4 evaluate()
    {
        return value + uint4(1, 2, 3, 4);
    }

    uint marker()
    {
        return 7;
    }
};

public struct MultiplyModel : IMaterialModel
{
    uint2 first;
    uint2 second;

    uint4 evaluate()
    {
        return uint4(first, second) * uint4(2, 3, 4, 5);
    }
};

public typedef AddModel AddAlias;
