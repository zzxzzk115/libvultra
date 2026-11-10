Shader "Tests/MaterialTypeConformances"
{
    Properties
    {
        model ("Model ID", Integer) = 3
        payload ("Payload", Vector) = (2, 3, 5, 7)
    }
    Variant "Default"
    {
        Modules { "type_conformance_models.slang" }
        Conformances
        {
            "AddModel" "IMaterialModel" = 3
            "MultiplyModel" "IMaterialModel" = 11
        }
    }
    Variant "Swapped"
    {
        Modules { "type_conformance_models.slang" }
        Defines { EXTRA_OUTPUT = 1 }
        Conformances
        {
            "AddModel" "IMaterialModel" = 11
            "MultiplyModel" "IMaterialModel" = 3
        }
    }
    Variant "OnlyAdd"
    {
        Modules { "type_conformance_models.slang" }
        Conformances { "AddModel" "IMaterialModel" = 3 }
    }
    SubShader
    {
        Pass
        {
            Name "Evaluate"
            Compute main
            SLANGPROGRAM
            import type_conformance_types;
            #ifndef OUTPUT_BIAS
            #define OUTPUT_BIAS 0
            #endif
            RWStructuredBuffer<float4> result;
            #if EXTRA_OUTPUT
            RWStructuredBuffer<float4> audit;
            #endif
            [numthreads(4, 1, 1)]
            void main(uint3 thread : SV_DispatchThreadID)
            {
                uint4 payload = uint4(material.payload) + thread.x;
                IMaterialModel model = createDynamicObject<IMaterialModel, uint4>(uint(material.model), payload);
                IMarker marker = createDynamicObject<IMarker, uint4>(3, payload);
                float4 value = float4(model.evaluate() + marker.marker() + OUTPUT_BIAS);
                result[thread.x] = value;
                #if EXTRA_OUTPUT
                audit[thread.x] = value + 100;
                #endif
            }
            ENDSLANG
        }
    }
}
