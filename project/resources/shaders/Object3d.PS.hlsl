#include "Object3d.hlsli"

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
    // 光沢度
    float32_t shininess;
    // 鏡面反射タイプ(0: None, 1: Phong, 2: Blinn-Phong)
    int32_t specularType;
};

// 平行光源情報
struct DirectionalLight {
    // ライトの色
    float32_t4 color;
    // ライトの向き
    float32_t3 direction;
    // 輝度
    float intensity;
};

// カメラ情報
struct Camera {
    // カメラのワールド座標
    float32_t3 worldPosition;
};

// 定数バッファ
ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);

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

    // ベースとなる色をまとめて計算しておく
    float32_t4 baseColor = gMaterial.color * textureColor * input.color;

    // 完全に透明なピクセルは描画を破棄する
    if (baseColor.a == 0.0f) {
        discard;
    }

    if (gMaterial.enableLighting != 0) {
        
        // 補間された法線を正規化
        float32_t3 normal = normalize(input.normal);
        
        // 物体表面からカメラへの方向ベクトルを算出する
        float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);

        // 鏡面反射の強度
        float specularPow = 0.0f;
        
        if (gMaterial.specularType == 1) {
            // Phong
            // 入射光の反射ベクトルを算出する
            float32_t3 reflectLight = reflect(gDirectionalLight.direction, normal);
            float RdotE = dot(reflectLight, toEye);
            specularPow = pow(saturate(RdotE), gMaterial.shininess);
        } else if (gMaterial.specularType == 2) {
            // Blinn-Phong
            // ハーフベクトルを算出する
            float32_t3 halfVector = normalize(-gDirectionalLight.direction + toEye);
            float NdotH = dot(normal, halfVector);
            specularPow = pow(saturate(NdotH), gMaterial.shininess);
        }

        // 拡散反射(Half-Lambert)
        float NdotL = dot(normal, -gDirectionalLight.direction);
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
        float32_t3 diffuse = baseColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;

        // 鏡面反射
        float32_t3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * float32_t3(1.0f, 1.0f, 1.0f);

        // 拡散反射 + 鏡面反射
        output.color.rgb = diffuse + specular;
        output.color.a = baseColor.a;
    } else {
        // ライティング無効時
        output.color = baseColor;
    }
    return output;
}