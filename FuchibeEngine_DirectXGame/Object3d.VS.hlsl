#include "Object3d.hlsli"

struct TransformationMatrix {
    float32_t4x4 wvp;
    float32_t4x4 world;
};

StructuredBuffer<TransformationMatrix> gTransformationMatrices : register(t0);

struct VertexShaderInput {
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
    
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrices[instanceId].wvp);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3) gTransformationMatrices[instanceId].world));
    
    return output;
};