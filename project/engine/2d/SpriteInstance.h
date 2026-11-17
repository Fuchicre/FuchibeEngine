#pragma once

// 前方宣言
class SpriteManager;

class SpriteInstance {
public:
	// 初期化関数
	void Initialize(SpriteManager* spriteManager);

public:
	SpriteManager* spriteManager = nullptr;

};