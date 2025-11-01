#include "pch.h"
#include "MusicPlayer.h"


int MusicPlayer::read_func(uint8_t* buf, int buf_size) {
	ATLTRACE("info: read buf_size=%d, rest=%lld\n", buf_size, file_stream->GetLength() - file_stream->GetPosition());
	// reset file_stream_end
	file_stream_end = false;
	int gcount = file_stream->Read(buf, buf_size);
	if (gcount == 0) {
		file_stream_end = true;
		return -1;
	}
	return gcount;
}

int MusicPlayer::read_func_wrapper(void* opaque, uint8_t* buf, int buf_size)
{
	auto callObject = reinterpret_cast<MusicPlayer*>(opaque);
	return callObject->read_func(buf, buf_size);
}

int64_t MusicPlayer::seek_func(int64_t offset, int whence)
{
	UINT origin;
	switch (whence) {
	case AVSEEK_SIZE: return static_cast<int64_t>(file_stream->GetLength());
	case SEEK_SET: origin = CFile::begin; break;
	case SEEK_CUR: origin = CFile::current; break;
	case SEEK_END: origin = CFile::end; break;
	default: return -1; // unsupported
	}
	ULONGLONG pos = file_stream->Seek(offset, origin);
	return static_cast<int64_t>(pos);
}

int64_t MusicPlayer::seek_func_wrapper(void* opaque, int64_t offset, int whence)
{
	auto callObject = reinterpret_cast<MusicPlayer*>(opaque);
	return callObject->seek_func(offset, whence);
}

inline int MusicPlayer::load_audio_context(CString audio_filename, CString file_extension)
{
	// 打开文件流
	// std::ios::sync_with_stdio(false);
	file_stream = new CFile();
	if (!file_stream->Open(audio_filename, CFile::modeRead | CFile::shareDenyWrite))
	{
		ATLTRACE("err: file not exists!\n");
		delete file_stream;
		return -1;
	}
	return load_audio_context_stream(file_stream);
}

int MusicPlayer::load_audio_context_stream(CFile* file_stream)
{
	if (!file_stream)
		return -1;

	// 重置文件流指针，防止读取后未复位
	file_stream->SeekToBegin();
	char* buf = DBG_NEW char[1024];
	memset(buf, 0, sizeof(buf));

	// 取得文件大小
	format_context = avformat_alloc_context();
	size_t file_len = static_cast<int64_t>(file_stream->GetLength());
	ATLTRACE("info: file loaded, size = %zu\n", file_len);

	constexpr size_t avio_buf_size = 8192;


	buffer = reinterpret_cast<unsigned char*>(av_malloc(avio_buf_size));
	avio_context =
		avio_alloc_context(buffer, avio_buf_size, 0,
			reinterpret_cast<void*>(this),
			&read_func_wrapper,
			nullptr,
			&seek_func_wrapper);

	format_context->pb = avio_context;

	// 打开音频文件
	int res = avformat_open_input(&format_context,
		nullptr, // dummy parameter, read from memory stream
		nullptr, // let ffmpeg auto detect format
		nullptr  // no parateter specified
	);
	if (res < 0) {
		av_strerror(res, buf, 1024);
		ATLTRACE("err: avformat_open_input failed: %s\n", buf);
		return -1;
	}
	if (!format_context)
	{
		av_strerror(res, buf, 1024);
		ATLTRACE("err: avformat_open_input failed, reason = %s(%d)\n", buf, res);
		release_audio_context();
		delete[] buf;
		return -1;
	}

	res = avformat_find_stream_info(format_context, nullptr);
	if (res == AVERROR_EOF)
	{
		ATLTRACE("err: no stream found in file\n");
		release_audio_context();
		delete[] buf;
		return -1;
	}
	ATLTRACE("info: stream count %d\n", format_context->nb_streams);
	audio_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO, -1, -1, const_cast<const AVCodec**>(&codec), 0);
	if (audio_stream_index < 0) {
		ATLTRACE("err: no audio stream found\n");
		release_audio_context();
		delete[] buf;
		return -1;
	}

	AVStream* current_stream = format_context->streams[audio_stream_index];
	codec = const_cast<AVCodec*>(avcodec_find_decoder(current_stream->codecpar->codec_id));
	if (!codec)
	{
		ATLTRACE("warn: no valid decoder found, stream id = %d!\n", audio_stream_index);
		release_audio_context();
		delete[] buf;
		return -1;
	}

	ATLTRACE("info: open stream id %d, format=%d, sample_rate=%d, channels=%d, channel_layout=%d\n",
		audio_stream_index,
		current_stream->codecpar->format,
		current_stream->codecpar->sample_rate,
		current_stream->codecpar->ch_layout.nb_channels,
		current_stream->codecpar->ch_layout.order);

	int image_stream_id = -1;

	for (unsigned int i = 0; i < format_context->nb_streams; i++) {
		AVStream* stream = format_context->streams[i];
		if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
			ATLTRACE("info: open stream id %d read attaching pic\n", i);
			image_stream_id = i;
			break;
		}
	}

	if (image_stream_id != -1) {
		album_art = decode_id3_album_art(image_stream_id);
	}

	AfxGetMainWnd()->PostMessage(WM_PLAYER_ALBUM_ART_INIT, reinterpret_cast<WPARAM>(album_art));
	read_metadata();

	// 从0ms开始读取
	avformat_seek_file(format_context, -1, INT64_MIN, 0, INT64_MAX, 0);
	// codec is not null
	// 建立解码器上下文
	codec_context = avcodec_alloc_context3(codec);
	if (codec_context == nullptr)
	{
		ATLTRACE("err: avcodec_alloc_context3 failed\n");
		release_audio_context();
		delete[] buf;
		return -1;
	}
	avcodec_parameters_to_context(codec_context, format_context->streams[audio_stream_index]->codecpar);

	// 解码文件
	res = avcodec_open2(codec_context, codec, nullptr);
	if (res)
	{
		av_strerror(res, buf, 1024);
		ATLTRACE("err: avcodec_open2 failed, reason = %s\n", buf);
		release_audio_context();
		delete[] buf;
		return -1;
	}

	// avoid ffmpeg warning
	codec_context->pkt_timebase = format_context->streams[audio_stream_index]->time_base;

	delete[] buf;
	return 0;
}

void MusicPlayer::release_audio_context()
{
	if (avio_context)
	{
		// 释放缓冲区上下文
		avio_context_free(&avio_context);
		avio_context = nullptr;
	}
	if (format_context)
	{
		// 释放文件解析上下文
		avformat_close_input(&format_context);
		format_context = nullptr;
	}

	if (codec_context)
	{
		// 释放解码器上下文
		avcodec_free_context(&codec_context);
		codec_context = nullptr;
	}
}

void MusicPlayer::reset_audio_context()
{
	release_audio_context();
	file_stream_end = false;
	load_audio_context_stream(file_stream);
}

bool MusicPlayer::is_audio_context_initialized()
{
	return avio_context
		&& format_context
		&& codec_context
		&& file_stream;
}

HBITMAP MusicPlayer::decode_id3_album_art(const int stream_index)
{
	if (!format_context) return nullptr;

	// stream_index = attached pic
	AVPacket pkt = format_context->streams[stream_index]->attached_pic;
	AVCodecParameters* codecpar = format_context->streams[stream_index]->codecpar;
	const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
	if (!codec) return nullptr;

	AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) return nullptr;

	// 
	if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}
	if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}
	// decode pipeline
	if (avcodec_send_packet(codec_ctx, &pkt) < 0) {
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}
	AVFrame* frame = av_frame_alloc();
	if (!frame) {
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}
	int ret = avcodec_receive_frame(codec_ctx, frame);
	if (ret < 0) {
		av_frame_free(&frame);
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}
	// bgr24 format
	SwsContext* sws_ctx = sws_getContext(
		frame->width, frame->height, (AVPixelFormat)frame->format,
		128, 128, AV_PIX_FMT_BGR24, // for display
		SWS_BILINEAR, nullptr, nullptr, nullptr
	);
	if (!sws_ctx) {
		av_frame_free(&frame);
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}
	AVFrame* rgb_frame = av_frame_alloc();
	if (!rgb_frame) {
		sws_freeContext(sws_ctx);
		av_frame_free(&frame);
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}
	int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, 128, 128, 1);
	uint8_t* buffer = (uint8_t*)av_malloc(num_bytes);
	if (!buffer) {
		av_frame_free(&rgb_frame);
		sws_freeContext(sws_ctx);
		av_frame_free(&frame);
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}
	rgb_frame->width = rgb_frame->height = 128;
	memcpy(rgb_frame->linesize, frame->linesize, sizeof(frame->linesize));
	av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, buffer, AV_PIX_FMT_BGR24, 128, 128, 1);
	sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, rgb_frame->data, rgb_frame->linesize);

	// negative avoid flip
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = rgb_frame->width;
	bmi.bmiHeader.biHeight = -static_cast<LONG>(rgb_frame->height); // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;
	bmi.bmiHeader.biCompression = BI_RGB;

	// copy
	void* bits = nullptr;
	HDC hdc = GetDC(nullptr);
	HBITMAP bitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	ReleaseDC(nullptr, hdc);

	if (!bitmap || !bits) {
		if (bitmap) DeleteObject(bitmap);
		av_free(buffer);
		av_frame_free(&rgb_frame);
		sws_freeContext(sws_ctx);
		av_frame_free(&frame);
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}

	int height = rgb_frame->height;
	int width = rgb_frame->width;
	int rowSize = width * 3; // 24bpp
	for (int y = 0; y < height; y++) {
		memcpy(reinterpret_cast<BYTE*>(bits) + y * rowSize, rgb_frame->data[0] + y * rgb_frame->linesize[0], rowSize);
	}

	av_free(buffer);
	av_frame_free(&rgb_frame);
	sws_freeContext(sws_ctx);
	av_frame_free(&frame);
	avcodec_free_context(&codec_ctx);

	return bitmap;
}

void MusicPlayer::read_metadata()
{
	auto convert_utf8 = [](const char* utf_8_str) {
		int len = MultiByteToWideChar(CP_UTF8, 0, utf_8_str, -1, nullptr, 0);
		std::wstring wtitle(len, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf_8_str, -1, &wtitle[0], len);
		return CString(wtitle.c_str());
	};
	auto read_metadata_iter = [&](AVDictionaryEntry* tag, CString& title, CString& artist) {
		CString key = convert_utf8(tag->key);
		CString value = convert_utf8(tag->value);
		ATLTRACE(_T("info: key %s = %s\n"), key.GetString(), value.GetString());
		if (!key.CompareNoCase(_T("title")) && song_title.IsEmpty()) {
			song_title = value;
			ATLTRACE(_T("info: song title: %s\n"), song_title.GetString());
		}
		else if (!key.CompareNoCase(_T("artist")) && song_artist.IsEmpty()) {
			song_artist = value;
			ATLTRACE(_T("info: song artist: %s\n"), song_artist.GetString());
		}
	};

	AVDictionaryEntry* tag = nullptr;
	while ((tag = av_dict_get(format_context->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
		read_metadata_iter(tag, song_title, song_artist);
	}

	// decode album title & artist
	for (unsigned int i = 0; i < format_context->nb_streams; i++) {
		AVStream* stream = format_context->streams[i];
		tag = nullptr;
		while ((tag = av_dict_get(stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
			read_metadata_iter(tag, song_title, song_artist);
		}
	}
}

// playback area
inline int MusicPlayer::initialize_audio_engine()
{
	// 初始化swscale
	auto stereo_layout = AVChannelLayout(AV_CHANNEL_LAYOUT_STEREO);
	if (!codec_context)
		return -1;

	swr_alloc_set_opts2(
		&swr_ctx,
		&stereo_layout,              // 输出立体声
		AV_SAMPLE_FMT_S16,
		44100,
		&codec_context->ch_layout,
		codec_context->sample_fmt,
		codec_context->sample_rate,
		0, nullptr
	);
	out_buffer = DBG_NEW uint8_t[8192];
	auto res = swr_init(swr_ctx);
	if (res < 0) {
		char* buf = DBG_NEW char[1024];
		memset(buf, 0, sizeof(buf));
		av_strerror(res, buf, 1024);
		ATLTRACE("err: swr_init failed, reason=%s\n", buf);
		delete[] buf;
		uninitialize_audio_engine();
		return -1;
	}

	// COM init in CMFCMusicPlayer.cpp

	// create com obj
	if (FAILED(XAudio2Create(&xaudio2)))
	{
		ATLTRACE("err: create xaudio2 com object failed\n");
		uninitialize_audio_engine();
		return -1;
	}

	// create master voice
	if (FAILED(xaudio2->CreateMasteringVoice(&mastering_voice))) {
		ATLTRACE("err: creating mastering voice failed\n");
		uninitialize_audio_engine();
		return -1;
	}

	// 创建source voice
	wfx.wFormatTag = WAVE_FORMAT_PCM;                     // pcm格式
	wfx.nChannels = 2;                                    // 音频通道数
	wfx.nSamplesPerSec = 44100;                           // 采样率
	wfx.wBitsPerSample = 16;  // xaudio2支持16-bit pcm，如果不符合格式的音频，使用swscale进行转码
	wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels; // 样本大小：样本大小(16-bit)*通道数
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign; // 每秒钟解码多少字节，样本大小*采样率
	wfx.cbSize = sizeof(wfx);
	if (FAILED(xaudio2->CreateSourceVoice(&source_voice, &wfx)))
	{
		ATLTRACE("err: create source voice failed\n");
		uninitialize_audio_engine();
		return -1;
	}
	frame = av_frame_alloc();
	packet = av_packet_alloc();

	return 0;
}

inline void MusicPlayer::uninitialize_audio_engine()
{
	// 等待xaudio线程执行完成
	if (audio_player_worker_thread
		&& audio_player_worker_thread->m_hThread != INVALID_HANDLE_VALUE)
	{
		InterlockedExchange(playback_state, audio_playback_state_stopped);
		DWORD exitCode;
		if (::GetExitCodeThread(audio_player_worker_thread->m_hThread, &exitCode)) {
			if (exitCode == STILL_ACTIVE) {
				WaitForSingleObject(audio_player_worker_thread->m_hThread, INFINITE);
			}
		}
		audio_player_worker_thread = nullptr;
		DeleteCriticalSection(audio_playback_section);
		delete  audio_playback_section;
		audio_playback_section = nullptr;
	}
	if (swr_ctx)
	{
		swr_close(swr_ctx);
		swr_free(&swr_ctx);
	}
	if (out_buffer)
	{
		delete[] out_buffer;
		out_buffer = nullptr;
	}
	if (source_voice) {
		source_voice->Stop(0);
		source_voice->FlushSourceBuffers();
		source_voice->DestroyVoice();
		source_voice = nullptr;
	}
	if (mastering_voice) {
		mastering_voice->DestroyVoice();
		mastering_voice = nullptr;
	}
	if (xaudio2) {
		xaudio2->Release();
		xaudio2 = nullptr;
	}
	if (frame) {
		av_frame_free(&frame);
		frame = nullptr;
	}
	if (packet) {
		av_packet_free(&packet);
		packet = nullptr;
	}
	// release xaudio2 buffer
	xaudio2_free_buffer();
	xaudio2_destroy_buffer();
}

void MusicPlayer::audio_playback_worker_thread()
{
	HRESULT hr;
	XAUDIO2_VOICE_STATE state;
	CEvent doneEvent;
	DWORD spinWaitResult;

	while (true) {
		EnterCriticalSection(audio_playback_section);
		if (file_stream_end)
			InterlockedExchange(playback_state, audio_playback_state_stopped);


		source_voice->GetState(&state);
		if (*playback_state ==
			audio_playback_state_stopped)
		{
			if (user_request_stop == true) {
				// immediate return
				ATLTRACE("info: user request stop, do cleaning\n");

				base_offset = state.SamplesPlayed;
				LeaveCriticalSection(audio_playback_section);
				break;
			}
			else if (file_stream_end && state.BuffersQueued > 0)
			{
				ATLTRACE("info: file stream ended, waiting for xaudio2 flush buffer\n");
				spinWaitResult = WaitForSingleObject(doneEvent, 1);
				if (spinWaitResult == WAIT_TIMEOUT) {
					source_voice->GetState(&state);
					elapsed_time = (state.SamplesPlayed - base_offset) * 1.0 / wfx.nSamplesPerSec + pts_seconds;
					ATLTRACE("info: samples played=%lld, elapsed time=%lf\n",
							state.SamplesPlayed, elapsed_time);

					UINT32 raw = *reinterpret_cast<UINT32*>(&elapsed_time);
					WPARAM w = static_cast<WPARAM>(raw);
					AfxGetMainWnd()->PostMessage(WM_PLAYER_TIME_CHANGE, (WPARAM)raw);
					continue;
				}
			}
			else
			{
				ATLTRACE("info: playback finished, destroying thread\n");
				AfxGetMainWnd()->PostMessage(WM_PLAYER_STOP);
				base_offset = state.SamplesPlayed;
				is_playing = false;
				elapsed_time = 0.0;
				UINT32 raw = *reinterpret_cast<UINT32*>(&elapsed_time);
				WPARAM w = static_cast<WPARAM>(raw);
				AfxGetMainWnd()->PostMessage(WM_PLAYER_TIME_CHANGE, (WPARAM)raw);
				LeaveCriticalSection(audio_playback_section);
				break; // 读取结束
			}
		}
		if (*playback_state == audio_playback_state_init
			&& is_pause) {
			if (av_seek_frame(format_context, -1, pts_seconds * AV_TIME_BASE, AVSEEK_FLAG_ANY) < 0) {
				ATLTRACE("err: resume failed\n");
				InterlockedExchange(playback_state, audio_playback_state_stopped);
			}
		}

		// 从输入文件中读取数据并解码
		if (av_read_frame(format_context, packet) < 0) {
			InterlockedExchange(playback_state, audio_playback_state_stopped);
			continue;
		}

		// ATLTRACE("info: selected package stream_index = %d\n", packet->stream_index);
		if (packet->stream_index == audio_stream_index) {
			int res = avcodec_send_packet(codec_context, packet);
			if (res < 0) {
				av_packet_unref(packet);
				continue; // 错误处理
			}

			while (true) {
				res = avcodec_receive_frame(codec_context, frame);
				// if (frame->pts != AV_NOPTS_VALUE) {
				// 	this->pts_seconds =
				// 		frame->pts * av_q2d(format_context->streams[audio_stream_index]->time_base); // recorded by FFmpeg
					// AfxGetMainWnd()->PostMessage(WM_PLAYER_TIME_CHANGE, reinterpret_cast<WPARAM>(&this->pts_seconds));
				// }
				if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
					break; // 没有更多帧
				}
				else if (res < 0) {
					ATLTRACE("err: avcodec_receive_frame failed\n");
					InterlockedExchange(playback_state, audio_playback_state_stopped);
					break;
				}

				// 创建输出缓冲区
				out_buffer_size = sizeof(uint8_t) * frame->nb_samples * wfx.nBlockAlign;
				if (out_buffer)
					delete[] out_buffer;
				out_buffer = DBG_NEW uint8_t[out_buffer_size];
				memset(out_buffer, 0, out_buffer_size);
				int out_samples = swr_convert(swr_ctx, &out_buffer, frame->nb_samples,
					(const uint8_t**)frame->data, frame->nb_samples);

				if (out_samples < 0) {
					ATLTRACE("err: swr_convert failed\n");
					av_frame_unref(frame);
					break;
				}

				while (state.BuffersQueued >= 64)
				{
					spinWaitResult = WaitForSingleObject(doneEvent, 1);
					if (spinWaitResult == WAIT_TIMEOUT) {
						source_voice->GetState(&state);
					}
				}

				// 将转换后的音频数据输出到xaudio2
				// TODO: Low-Perfomance Module (use AVAudioFifo for better experience)
				XAUDIO2_BUFFER* buffer = xaudio2_get_available_buffer(out_samples * wfx.nBlockAlign);
				buffer->AudioBytes = out_samples * wfx.nBlockAlign; // 每样本2字节，每声道2字节
				memcpy(const_cast<BYTE*>(buffer->pAudioData), out_buffer, buffer->AudioBytes);

				hr = source_voice->SubmitSourceBuffer(buffer);
				if (FAILED(hr)) {
					ATLTRACE("err: submit source buffer failed, reason=0x%x\n", hr);
					InterlockedExchange(playback_state, audio_playback_state_stopped);
					break;
				}

				source_voice->GetState(&state);
				// std::printf("info: submitted source buffer, buffers queued=%d\n", state.BuffersQueued);

				// 播放音频
				// source_voice->GetState(&state);
				if (*playback_state == audio_playback_state_init)
				{
					// if (state.BuffersQueued == 32)
					// {
					InterlockedExchange(playback_state, audio_playback_state_playing);
					source_voice->Start();
					AfxGetMainWnd()->PostMessage(WM_PLAYER_START);
					// }
				}
				else
				{
					auto samples_played_before = state.SamplesPlayed;
					auto samples_sum = xaudio2_played_samples;
					auto played_buffers = xaudio2_played_buffers; auto it = xaudio2_playing_buffers.begin();
					bool flag = false;
					while (it != xaudio2_playing_buffers.end())
					{
						XAUDIO2_BUFFER*& played_buffer = *it;
						played_buffers++;
						samples_sum += played_buffer->AudioBytes / wfx.nBlockAlign;
						if (samples_sum >= state.SamplesPlayed)
						{
							flag = true;
							break;
						}
						else
							++it;
					}

					if (it != xaudio2_playing_buffers.begin())
					{
						// --it;
						xaudio2_free_buffers.insert(xaudio2_free_buffers.end(),
							xaudio2_playing_buffers.begin(), it);
						xaudio2_playing_buffers.erase(xaudio2_playing_buffers.begin(), it);
						xaudio2_played_buffers = played_buffers - 1;
						xaudio2_played_samples = samples_sum - (*it)->AudioBytes / wfx.nBlockAlign;
						ATLTRACE("info: samples played=%lld, cur played_buffers=%lld, cur samples=%lld, xaudio2 buffer arr size=%lld\n",
							state.SamplesPlayed, played_buffers, samples_sum, xaudio2_playing_buffers.size());
						elapsed_time = (xaudio2_played_samples - base_offset) * 1.0 / wfx.nSamplesPerSec + this->pts_seconds;
						UINT32 raw = *reinterpret_cast<UINT32*>(&elapsed_time);
						WPARAM w = static_cast<WPARAM>(raw);
						AfxGetMainWnd()->PostMessage(WM_PLAYER_TIME_CHANGE, w);
						// std::printf("info: buffer played=%zd\n", played_buffers);
					}
					// else
					// {
						// std::printf("info: buffer played=%zd\n", xaudio2_played_buffers);
					// }
					//  (wfx.wBitsPerSample / 8) * wfx.nChannels
					av_frame_unref(frame);
				}
			}
		}
		av_packet_unref(packet);
		LeaveCriticalSection(audio_playback_section);
	}
}

inline void MusicPlayer::start_audio_playback()
{
	if (*playback_state == audio_playback_state_stopped) {
		reset_audio_context();
	}
	InterlockedExchange(playback_state, audio_playback_state_init);
	audio_playback_section = new CRITICAL_SECTION;
	InitializeCriticalSection(audio_playback_section);
	audio_player_worker_thread = AfxBeginThread(
		[](LPVOID param) -> UINT {
			auto player = reinterpret_cast<MusicPlayer*>(param);
			player->audio_playback_worker_thread();
			return 0;
		},
		reinterpret_cast<LPVOID>(this),
		THREAD_PRIORITY_ABOVE_NORMAL,
		0,
		CREATE_SUSPENDED,
		nullptr);
	ATLTRACE("info: thread created, handle = %p\n", static_cast<void*>(audio_player_worker_thread));
	audio_player_worker_thread->m_bAutoDelete = true;
	audio_player_worker_thread->ResumeThread();
	is_playing = true; user_request_stop = false;
}

void MusicPlayer::stop_audio_playback(int mode)
{
	if (is_playing) {
		while (!TryEnterCriticalSection(audio_playback_section));
		// EnterCriticalSection(audio_playback_section); <- this cause delay, spin wait instead
		user_request_stop = true;
		InterlockedExchange(playback_state, audio_playback_state_stopped);
		avcodec_flush_buffers(codec_context);
		avcodec_free_context(&codec_context);
		avformat_close_input(&format_context);
		source_voice->Stop(0);
		source_voice->FlushSourceBuffers();
		is_playing = false;
		LeaveCriticalSection(audio_playback_section);
		// wait for thread to terminate
		WaitForSingleObject(audio_player_worker_thread->m_hThread, INFINITE);
		// managed by mfc
		audio_player_worker_thread = nullptr;
		DeleteCriticalSection(audio_playback_section);
		delete audio_playback_section;
		audio_playback_section = nullptr;
	}
	// terminated xaudio and ffmpeg, do cleanup
	xaudio2_free_buffer();
	if (mode == 0)
		reset_audio_context();
	else if (mode == -1)
		release_audio_context();
	is_playing = false;
}

inline const char* MusicPlayer::get_backend_implement_version()
{
	static char xaudio2_implement_version[] = XAUDIO2_DLL_A;
	return xaudio2_implement_version;
}

void MusicPlayer::xaudio2_init_buffer(XAUDIO2_BUFFER* dest_buffer, int size)
{
	if (size < 8192) size = 8192;
	int& buffer_size = *reinterpret_cast<int*>(dest_buffer->pContext);
	if (size > buffer_size)
	{
		ATLTRACE("info: xaudio2 reallocate_buffer, reallocate_size=%d, original_size=%d\n", size, buffer_size);
		delete[] dest_buffer->pAudioData;
		dest_buffer->pAudioData = DBG_NEW BYTE[size];
		buffer_size = size;
	}
	memset(const_cast<BYTE*>(dest_buffer->pAudioData), 0, size);
}

XAUDIO2_BUFFER* MusicPlayer::xaudio2_allocate_buffer(int size)
{
	if (size < 8192) size = 8192;
	std::printf("info: xaudio2_allocate_buffer, allocate_size=%d\n", size);
	XAUDIO2_BUFFER* dest_buffer = DBG_NEW XAUDIO2_BUFFER{};
	dest_buffer->pAudioData = DBG_NEW BYTE[size];
	dest_buffer->pContext = DBG_NEW int(size);
	xaudio2_init_buffer(dest_buffer);
	return dest_buffer;
}

XAUDIO2_BUFFER* MusicPlayer::xaudio2_get_available_buffer(int size)
{
	// std::printf("info: DBG_NEW xaudio2_buffer request, allocated=%lld, played=%lld\n", xaudio2_allocated_buffers, xaudio2_played_buffers);
	if (xaudio2_free_buffers.size() > 0)
	{
		// std::printf("info: free buffer recycled\n");
		auto dest_buffer = xaudio2_free_buffers.front();
		xaudio2_free_buffers.pop_front();
		xaudio2_init_buffer(dest_buffer, size);
		xaudio2_playing_buffers.push_back(dest_buffer);
		return dest_buffer;
	}
	// Allocate a DBG_NEW XAudio2 buffer.
	xaudio2_playing_buffers.push_back(xaudio2_allocate_buffer(size));
	xaudio2_allocated_buffers++;
	// std::printf("info: DBG_NEW xaudio2 buffer allocated, current allocate: %lld\n", xaudio2_allocated_buffers);
	return xaudio2_playing_buffers.back();
}

void MusicPlayer::xaudio2_free_buffer()
{
	for (auto& i : xaudio2_playing_buffers)
	{
		assert(i);
		delete[] i->pAudioData;
		delete i->pContext;
		delete i;
		i = nullptr;
	}
	xaudio2_allocated_buffers = 0; xaudio2_played_buffers = 0;
	xaudio2_playing_buffers.clear();
}

void MusicPlayer::xaudio2_destroy_buffer()
{
	for (auto& i : xaudio2_free_buffers)
	{
		assert(i);
		delete[] i->pAudioData;
		delete i->pContext;
		delete i;
		i = nullptr;
	}
	xaudio2_free_buffers.clear();
}

bool MusicPlayer::is_xaudio2_initialized()
{
	return swr_ctx && out_buffer && source_voice && mastering_voice && xaudio2;
}

MusicPlayer::MusicPlayer() :
	xaudio2_buffer_ended(DBG_NEW volatile unsigned long long),
	playback_state(DBG_NEW volatile unsigned long long),
	audio_position(DBG_NEW volatile unsigned long long),
	audio_playback_section(nullptr)
{
	ATLTRACE("info: decode frontend: avformat version %d, avcodec version %d, avutil version %d, swresample version %d\n",
		avformat_version(),
		avcodec_version(),
		avutil_version(),
		swresample_version());
	ATLTRACE("info: audio api backend: XAudio2 version %s\n", get_backend_implement_version());
}

bool MusicPlayer::IsInitialized()
{
	return is_audio_context_initialized() && is_xaudio2_initialized();
}

bool MusicPlayer::IsPlaying()
{
	return is_playing && (*playback_state == audio_playback_state_playing);
}

void MusicPlayer::OpenFile(CString fileName, CString file_extension)
{
	if (load_audio_context(fileName, file_extension)) {
		AfxMessageBox(_T("err: load file failed, please check trace message!"), MB_ICONERROR);
		return;
	}
	if (initialize_audio_engine()) {
		AfxMessageBox(_T("err: audio engine initialize failed!"), MB_ICONERROR);
		return;
	};
}

float MusicPlayer::GetMusicTimeLength()
{
	if (IsInitialized()) {
		if (fabs(length - 0.0f) < 0.0001f) {
			AVStream* audio_stream = format_context->streams[audio_stream_index];
			int64_t duration = audio_stream->duration;
			AVRational time_base = audio_stream->time_base;
			length = duration * av_q2d(time_base);
		}
		return length;
	}
	return 0.0f;
}

CString MusicPlayer::GetSongTitle()
{
	if (IsInitialized()) {
		return song_title;
	}
	return CString();
}

CString MusicPlayer::GetSongArtist()
{
	if (IsInitialized()) {
		return song_artist;
	}
	return CString();
}

void MusicPlayer::Start()
{
	if (IsInitialized() && !IsPlaying()) {
		start_audio_playback();
	}
}

void MusicPlayer::Stop()
{
	if (IsInitialized() && IsPlaying()) {
		pts_seconds = 0;
		stop_audio_playback(0);
	}
}

void MusicPlayer::Pause()
{
	if (IsInitialized() && IsPlaying()) {
		is_pause = true;
		pts_seconds = elapsed_time;
		stop_audio_playback(0);
	}
}

MusicPlayer::~MusicPlayer()
{
	if (is_playing)
		stop_audio_playback(-1);
	uninitialize_audio_engine();

	if (xaudio2_buffer_ended)	delete xaudio2_buffer_ended;
	if (playback_state)			delete playback_state;
	if (audio_position)			delete audio_position;

	if (file_stream)
	{
		file_stream->Close();
		delete file_stream;
		file_stream = nullptr;
	}
}
