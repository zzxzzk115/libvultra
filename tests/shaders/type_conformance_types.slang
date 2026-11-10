public interface IMaterialModel
{
    uint4 evaluate();
};

public interface IMarker
{
    uint marker();
};

public struct PlainPayload
{
    uint4 value;
};

public typedef IMaterialModel MaterialInterfaceAlias;
