#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include "WindowsApi.h"
#include <array>

class DirectXPlatform {
public:
	// 初期化関数 (ハイフンと改行を修正)
	void Initialize(WindowsApi* windowsApi);

	// SRVの指定番号のCPUデスクリプタハンドルを取得する関数
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(uint32_t index);

	// SRVの指定番号のGPUデスクリプタハンドルを取得する関数
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t index);

private:
	// デバイスの生成関数
	void CreateDevice();

	// コマンド関連の初期化関数
	void CommandInitialize();

	// スワップチェーンの生成関数
	void CreateSwapChain();

	// 深度バッファの生成関数
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthBuffer(
		const Microsoft::WRL::ComPtr<ID3D12Device>& device,
		int32_t width,
		int32_t height);

	// 各種デスクリプタヒープ・テクスチャの生成関数
	void CreateDepthStencilTexture();

	// デスクリプタヒープの生成関数
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE heapType,
		UINT numDescriptors,
		bool shaderVisible);

	// レンダーターゲットビューの初期化関数
	void RenderTargetViewInitialize();

	// 深度ステンシルビューの初期化関数
	void DepthStencilViewInitialize();

	// フェンスの初期化関数
	void FenceInitialize();

	// ビューポート矩形の初期化関数
	void ViewportRectInitialize();

	// シザリング矩形の初期化関数
	void ScissorRectInitialize();

	// DXCコンパイラの生成関数
	void CreateDxcCompiler();

	// ImGuiの初期化関数
	void ImGuiInitialize();

	// DescriptorCPUHandle取得関数
	static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
		const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
		uint32_t descriptorSize,
		uint32_t index);

	// DescriptorGPUHandle取得関数
	static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
		const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
		uint32_t descriptorSize,
		uint32_t index);

private:
	// WindowsAPI (ハイフンと型名の繋がりを修正)
	WindowsApi* windowsApi = nullptr;

	// DirectX12デバイス
	Microsoft::WRL::ComPtr<ID3D12Device> device;

	// DXGIファクトリ
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;

	// コマンドキュー
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;

	// コマンドアロケータ
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;

	// コマンドリスト
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;

	uint32_t descriptorSizeRTV = 0;
	uint32_t descriptorSizeSRV = 0;
	uint32_t descriptorSizeDSV = 0;

	// スワップチェーン
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> swapChainResources;

	// 各種デスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;

	// DepthStencilResource
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource;

	// フェンス
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;

	static constexpr UINT kBufferCount = 2;
	static constexpr DXGI_FORMAT kRtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
};