#include "Particle.hlsli"

// パーティクルごとの変換行列・色情報
struct ParticleForGPU {
    // worldViewProjection行列
    float32_t4x4 wvp;
    // ワールド行列
    float32_t4x4 world;
    // パーティクルの色
    float32_t4 color;
};

// パーティクル用の構造化バッファ
StructuredBuffer<ParticleForGPU> gParticle : register(t0);

// 頂点シェーダーの入力
struct VertexShaderInput {
    // ローカル座標
    float32_t4 position : POSITION0;
    // UV座標
    float32_t2 texcoord : TEXCOORD0;
    // ローカル法線
    float32_t3 normal : NORMAL0;
};

// 頂点シェーダーの出力
struct VertexShaderOutput {
    // クリップ空間の頂点座標
    float32_t4 position : SV_POSITION;
    // UV座標
    float32_t2 texcoord : TEXCOORD0;
    // ワールド空間の法線
    float32_t3 normal : NORMAL0;
    // パーティクルごとの色
    float32_t4 color : COLOR0;
};

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;

    // クリップ空間座標の算出とUV座標の受け渡し
    output.position = mul(input.position, gParticle[instanceId].wvp);
    output.texcoord = input.texcoord;

    // 法線をワールド空間に変換して正規化(平行移動成分を除くため3x3行列を使用する)
    output.normal = normalize(mul(input.normal, (float32_t3x3) gParticle[instanceId].world));

    // パーティクルごとの色を渡す
    output.color = gParticle[instanceId].color;

    return output;
}