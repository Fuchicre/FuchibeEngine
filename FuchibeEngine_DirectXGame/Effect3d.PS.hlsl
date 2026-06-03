struct Material
{
    float4 color;
};

ConstantBuffer<Material> gMaterial : register(b0);

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(VertexShaderOutput input) : SV_TARGET
{
    return gMaterial.color;
}