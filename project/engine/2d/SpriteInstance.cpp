#include "SpriteInstance.h"
#include "SpriteManager.h"
#include <wrl.h>

using namespace Microsoft::WRL;

// 初期化関数
void SpriteInstance::Initialize(SpriteManager* spriteManager) {

	// 引数で受け取ってメンバ変数に記録する
	this->spriteManager = spriteManager;

}