#pragma once
#include <d3d12.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <fstream>
#include <string>
#include <cstdint>
#include <cassert>
#pragma comment(lib, "xaudio2.lib")

class Audio {
public:
	// チャンクヘッダ
	struct ChunkHeader {
		// チャンク用のID
		char id[4];
		// チャンクサイズ
		int32_t size;
	};

	// RIFFヘッダチャンク
	struct RiffHeader {
		// "RIFF"
		ChunkHeader chunk;
		// "WAVE"
		char type[4];
	};

	// FMTチャンク
	struct FormatChunk {
		// "fmt"
		ChunkHeader chunk;
		// 波形フォーマット
		WAVEFORMATEX fmt;
	};

	// 音声データ
	struct SoundData {
		// 波形フォーマット
		WAVEFORMATEX wfex;
		// バッファの先頭アドレス
		BYTE* pBuffer;
		// バッファのサイズ
		unsigned int bufferSize;
		// 再生制御用のソースボイスポインタ
		IXAudio2SourceVoice* pSourceVoice;
	};

public:
	// シングルトンインスタンスの取得
	static Audio* GetInstance();

	// 初期化
	void Initialize();

	// Finalize関数(オーディオシステムの解放)
	void Finalize();

	//===========================================
	// SoundLoadWave関数(音声データを読み込む関数)
	//===========================================

	SoundData SoundLoadWave(const char* filename);

	//=========================================
	// SoundUnload関数(音声データを解放する関数)
	//=========================================

	void SoundUnload(SoundData* soundData);

	//======================================
	// SoundPlayWave関数(音声を再生する関数)
	//======================================

	void SoundPlayWave(const SoundData& soundData);

private:
	Audio() = default;
	~Audio() = default;
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;

private:
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masterVoice = nullptr;
};