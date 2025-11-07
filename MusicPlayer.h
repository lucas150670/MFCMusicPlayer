#pragma once

#include "framework.h"

class MusicPlayer
{
	// 流文件解析上下文
	AVFormatContext* format_context = nullptr;
	// 针对该文件，找到的解码器类型
	AVCodec* codec = nullptr;
	// 使用的解码器实例
	AVCodecContext* codec_context = nullptr;
	// 解码前的数据（流中的一个packet）
	AVPacket* packet = nullptr;
	// 解码后的数据（一帧数据）
	AVFrame* frame = nullptr;
	// 音频流编号
	unsigned audio_stream_index = static_cast<unsigned>(-1); // inf
	AVIOContext* avio_context = nullptr;
	unsigned char* buffer = nullptr;

	CString file_extension;
	CFile* file_stream = nullptr;
	bool file_stream_end = false;
	bool user_request_stop = false;
	double pts_seconds = 0.0;
	float elapsed_time = 0.0;
	float length = 0.0f;
	bool is_pause = false;
	bool is_playing = false;
	bool decode_in_background = false;
	bool decoder_is_running = false;
	HBITMAP album_art = nullptr;
	CString song_title = {};
	CString song_artist = {};

	CRITICAL_SECTION* audio_fifo_section;
	CRITICAL_SECTION* audio_playback_section;

	IXAudio2* xaudio2 = nullptr;
	IXAudio2MasteringVoice* mastering_voice = nullptr;
	IXAudio2SourceVoice* source_voice = nullptr;
	SwrContext* swr_ctx = nullptr;
	WAVEFORMATEX wfx = {};

	volatile unsigned long long* xaudio2_buffer_ended;
	volatile unsigned long long* playback_state;
	volatile unsigned long long* audio_position;
	CWinThread* audio_player_worker_thread = nullptr;
	CWinThread* audio_decoder_worker_thread = nullptr;

	std::list<XAUDIO2_BUFFER*> xaudio2_playing_buffers = {};
	std::list<XAUDIO2_BUFFER*> xaudio2_free_buffers = {};
	size_t xaudio2_played_buffers = 0, xaudio2_allocated_buffers = 0, xaudio2_played_samples = 0;
	uint8_t* out_buffer = nullptr;
	size_t out_buffer_size = 0, base_offset = 0;

	// use avaudiofifo to avoid lag on low-cpu performance system, like jasper lake/alder lake-n
	AVAudioFifo* audio_fifo = nullptr;
	int xaudio2_play_frame_size = 256;
	LPDWORD xaudio2_thread_task_index = nullptr;
	HANDLE frame_ready_event = nullptr;
	HANDLE frame_underrun_event = nullptr;
	bool decode_lag_use_big_buffer = false;
	double standard_frametime = 0.0, last_frametime = 0.0;

	// file I/O Area
	int read_func(uint8_t* buf, int buf_size);
	static int read_func_wrapper(void* opaque, uint8_t* buf, int buf_size);
	int64_t seek_func(int64_t offset, int whence);
	static int64_t seek_func_wrapper(void* opaque, int64_t offset, int whence);
	int load_audio_context(CString audio_filename, CString file_extension = CString());
	int load_audio_context_stream(CFile* file_stream);
	void release_audio_context();
	void reset_audio_context();
	bool is_audio_context_initialized();
	HBITMAP decode_id3_album_art(const int stream_index, int scale_size = 128);
	void read_metadata();

	// playback area
	int initialize_audio_engine();
	void init_decoder_thread();
	void uninitialize_audio_engine();
	void audio_playback_worker_thread();
	// todo: separate audio decode & buffer submission
	void audio_decode_worker_thread();
	void start_audio_playback();
	void stop_audio_decode();
	void stop_audio_playback(int mode);

	int initialize_audio_fifo(AVSampleFormat sample_fmt, int channels, int nb_samples);
	int resize_audio_fifo(int nb_samples);
	int add_samples_to_fifo(uint8_t** decoded_data, int nb_samples);
	int read_samples_from_fifo(uint8_t** output_buffer, int nb_samples);
	void drain_audio_fifo(int nb_samples);
	void reset_audio_fifo();
	int get_audio_fifo_cached_samples_size();
	void uninitialize_audio_fifo();

	// XAudio2 helper function
	const char* get_backend_implement_version();
	void xaudio2_init_buffer(XAUDIO2_BUFFER* dest_buffer, int size = 8192);
	XAUDIO2_BUFFER* xaudio2_allocate_buffer(int size = 8192);
	XAUDIO2_BUFFER* xaudio2_get_available_buffer(int size = 8192);
	void xaudio2_free_buffer();
	void xaudio2_destroy_buffer();
	int decoder_query_xaudio2_buffer_size();
	bool is_xaudio2_initialized();
	size_t get_samples_played_per_session();
	void dialog_ffmpeg_critical_error(int err_code, const char* file, int line);

public:
	// constructor
	MusicPlayer();
	// no copy & move
	MusicPlayer(const MusicPlayer&) = delete;
	MusicPlayer& operator=(const MusicPlayer&) = delete;
	MusicPlayer(MusicPlayer&&) = delete;
	MusicPlayer& operator=(MusicPlayer&&) = delete;

	// Interfaces
	bool IsInitialized();
	bool IsPlaying();
	void OpenFile(CString fileName, CString file_extension = CString());
	float GetMusicTimeLength();
	CString GetSongTitle();
	CString GetSongArtist();
	void Start();
	void Pause();
	void Stop();
	void SetMasterVolume(float volume);
	// void SeekToPosition(float time, bool needStop);

	// destructor
	~MusicPlayer();
};

