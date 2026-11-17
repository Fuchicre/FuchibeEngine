#include "Particle.hlsli"

// テクスチャとサンプラー
Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// マテリアル情報
struct Material {
    // 基本色
    float32_t4 color;
    // ライティング有効フラグ
    int32_t enableLighting;
    // UV変換行列
    float32_t4x4 uvTransform;
};

// 定数バッファ
ConstantBuffer<Material> gMaterial : register(b0);

// ピクセルシェーダーの出力
struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV座標を変換してテクスチャから色を取得する
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // マテリアル色・テクスチャ色・パーティクル色を乗算して最終的な色を算出する
    output.color = gMaterial.color * textureColor * input.color;

    // 完全に透明なピクセルは描画を破棄する
    if (output.color.a == 0.0f) {
        discard;
    }
    return output;
}