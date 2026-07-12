#include "Audio.h"

Audio* Audio::GetInstance() {
	static Audio instance;
	return &instance;
}

void Audio::Initialize() {

	HRESULT hr;

	//======================
	// XAudio2の初期化
	//======================

	// XAudio2エンジンのインスタンスを生成
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(hr));

	// マスターボイスの作成
	masterVoice = nullptr;

	hr = xAudio2->CreateMasteringVoice(&masterVoice);
	assert(SUCCEEDED(hr));
}

// Finalize関数(オーディオシステムの解放)
void Audio::Finalize() {

	if (masterVoice) {
		masterVoice->DestroyVoice();
		masterVoice = nullptr;
	}

	xAudio2.Reset();
}

//===========================================
// SoundLoadWave関数(音声データを読み込む関数)
//===========================================

#pragma region SoundLoadWave関数(音声データを読み込む関数)

Audio::SoundData Audio::SoundLoadWave(const char* filename) {

	// ①ファイルオープン //

	// ファイル入力ストリームのインスタンス
	std::ifstream file;

	// .wavファイルをバイナリモードで開く
	file.open(filename, std::ios_base::binary);

	// ファイルオープン失敗を検知する
	assert(file.is_open());

	// ②.wavデータの読み込み //

	// RIFFヘッダの読み込み
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));

	// ファイルがRIFFかチェック
	if (strncmp(riff.chunk.id, "RIFF", 4) != 0) {
		assert(0);
	}

	// ファイルがWAVEかチェック
	if (strncmp(riff.type, "WAVE", 4) != 0) {
		assert(0);
	}

	// フォーマットチャンクの読み込み
	FormatChunk format = {};

	// チャンクヘッダの確認
	file.read((char*)&format, sizeof(ChunkHeader));

	if (strncmp(format.chunk.id, "fmt ", 4) != 0) {
		assert(0);
	}

	// チャンク本体の読み込み
	assert(format.chunk.size <= sizeof(format.fmt));
	file.read((char*)&format.fmt, format.chunk.size);

	// Dataチャンクの読み込み(dataチャンクが見つかるまでループする)
	ChunkHeader data;

	while (true) {

		file.read((char*)&data, sizeof(data));

		// ファイルの終端に達してしまった場合はエラー
		if (file.eof()) {
			assert(0 && "dataチャンクが見つかりませんでした");
		}

		// "data" チャンクが見つかったらループを抜ける
		if (strncmp(data.id, "data", 4) == 0) {
			break;
		}

		// "data" 以外("JUNK", "LIST" など)はサイズ分だけ読み飛ばす
		file.seekg(data.size, std::ios_base::cur);
	}

	// Dataチャンクのデータ部(波形データ)の読み込み
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);

	// ③WAVEファイルを閉じる //
	file.close();

	// ④読み込んだ音声データをreturn //

	// returnするための音声データ
	SoundData soundData = {};

	soundData.wfex = format.fmt;
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
	soundData.bufferSize = data.size;
	soundData.pSourceVoice = nullptr;

	return soundData;
}
#pragma endregion

//=========================================
// SoundUnload関数(音声データを解放する関数)
//=========================================

#pragma region SoundUnload関数(音声データを解放する関数)

void Audio::SoundUnload(SoundData* soundData) {

	// ボイスが残っていれば破棄する安全処理
	if (soundData->pSourceVoice) {
		soundData->pSourceVoice->DestroyVoice();
		soundData->pSourceVoice = nullptr;
	}

	// バッファのメモリを解放
	delete[] soundData->pBuffer;

	soundData->pBuffer = 0;
	soundData->bufferSize = 0;
	soundData->wfex = {};
}
#pragma endregion

//======================================
// SoundPlayWave関数(音声を再生する関数)
//======================================

#pragma region SoundPlayWave関数(音声を再生する関数)

void Audio::SoundPlayWave(const SoundData& soundData) {

	HRESULT result;

	// 多重再生や安全なインスタンス保持のため、constを外してクラス管理のポインタに割り当てる
	SoundData& targetData = const_cast<SoundData&>(soundData);

	// 波形フォーマットを元にSoundVoiceの生成
	result = xAudio2->CreateSourceVoice(&targetData.pSourceVoice, &soundData.wfex);
	assert(SUCCEEDED(result));

	// 再生する波形データの設定
	XAUDIO2_BUFFER buffer{};
	buffer.pAudioData = soundData.pBuffer;
	buffer.AudioBytes = soundData.bufferSize;
	buffer.Flags = XAUDIO2_END_OF_STREAM;

	// 波形データの再生
	result = targetData.pSourceVoice->SubmitSourceBuffer(&buffer);
	assert(SUCCEEDED(result));
	result = targetData.pSourceVoice->Start();
	assert(SUCCEEDED(result));
}
#pragma endregion