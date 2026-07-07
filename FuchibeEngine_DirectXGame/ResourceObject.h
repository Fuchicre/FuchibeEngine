#pragma once
#include <d3d12.h>
#include <wrl/client.h>

class ResourceObject {

public:

	ResourceObject(ID3D12Resource* resource)
		:mResource(resource)
	{}

	// ComPtrを受け取るコンストラクタ
	// 関数から戻ってきたComPtrから生ポインタをデタッチ(移動)して管理を引き継ぐ
	ResourceObject(Microsoft::WRL::ComPtr<ID3D12Resource>&& resource)
		: mResource(resource.Detach()) {
	}

	~ResourceObject() {
		if (mResource) {
			mResource->Release();
		}
	}

	// コピーコンストラクタを禁止
	ResourceObject(const ResourceObject&) = delete;

	// コピー代入演算子を禁止
	ResourceObject& operator=(const ResourceObject&) = delete;

	ID3D12Resource* Get() { return mResource; }

private:

	ID3D12Resource* mResource;

};