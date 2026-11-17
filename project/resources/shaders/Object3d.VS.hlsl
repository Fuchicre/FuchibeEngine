#include "Object3d.hlsli"

// インスタンスごとの変換行列・色情報
struct TransformationMatrix {
    // worldViewProjection行列
    float32_t4x4 wvp;
    // ワールド行列
    float32_t4x4 world;
    // オブジェクトの色
    float32_t4 color;
};

// インスタンシング用の構造化バッファ
StructuredBuffer<TransformationMatrix> gTransformationMatrices : register(t0);

// 頂点シェーダーの入力
struct VertexShaderInput {
    // ローカル座標
    float32_t4 position : POSITION0;
    // UV座標
    float32_t2 texcoord : TEXCOORD0;
    // ローカル法線
    float32_t3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;

    // クリップ空間座標の算出とUV座標の受け渡し
    output.position = mul(input.position, gTransformationMatrices[instanceId].wvp);
    output.texcoord = input.texcoord;

    // 法線をワールド空間に変換して正規化(平行移動成分を除くため3x3行列を使用する)
    output.normal = normalize(mul(input.normal, (float32_t3x3) gTransformationMatrices[instanceId].world));

    // インスタンスごとの色を渡す
    output.color = gTransformationMatrices[instanceId].color;

    // 鏡面反射計算用にワールド座標をピクセルシェーダーへ渡す
    output.worldPosition = mul(input.position, gTransformationMatrices[instanceId].world).xyz;

    return output;
}