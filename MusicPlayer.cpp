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
		album_art = decode_id3_album_art(image_stream_id, 160);
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
	// set parallel decode (flac, wav..
	av_opt_set_int(codec_context, "threads", 0, 0);

	// init avaudiofifo
	if (!audio_fifo) {
		res = initialize_audio_fifo(codec_context->sample_fmt,
			codec_context->ch_layout.nb_channels,
			1024); // initial size
		if (res < 0) {
			ATLTRACE("err: initialize_audio_fifo failed\n");
			release_audio_context();
			delete[] buf;
			return -1;
		}
	}
	delete[] buf;

	// init decoder
	frame = av_frame_alloc();
	packet = av_packet_alloc();
	decoder_audio_channels = codec_context->ch_layout.nb_channels;
	decoder_audio_sample_fmt = codec_context->sample_fmt;
	init_decoder_thread();
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
	uninitialize_audio_fifo();
}

void MusicPlayer::reset_audio_context()
{
	release_audio_context();
	file_stream_end = false;
	InterlockedExchange(playback_state, audio_playback_state_init);
	reset_audio_fifo();
	load_audio_context_stream(file_stream);
}

bool MusicPlayer::is_audio_context_initialized()
{
	return avio_context
		&& format_context
		&& codec_context
		&& file_stream;
}

HBITMAP MusicPlayer::decode_id3_album_art(const int stream_index, int scale_size)
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
	// bgr24 format (windows bmp use this), allow ui to select output size
	SwsContext* sws_ctx = sws_getContext(
		frame->width, frame->height, (AVPixelFormat)frame->format,
		scale_size, scale_size, AV_PIX_FMT_BGR24, // for display
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
	int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, scale_size, scale_size, 1);
	uint8_t* buffer = (uint8_t*)av_malloc(num_bytes);
	if (!buffer) {
		av_frame_free(&rgb_frame);
		sws_freeContext(sws_ctx);
		av_frame_free(&frame);
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}
	rgb_frame->width = rgb_frame->height = scale_size;
	memcpy(rgb_frame->linesize, frame->linesize, sizeof(frame->linesize));
	av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, buffer, AV_PIX_FMT_BGR24, scale_size, scale_size, 1);
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
	if (FAILED(xaudio2->CreateMasteringVoice(&mastering_voice,
		XAUDIO2_DEFAULT_CHANNELS,
		XAUDIO2_DEFAULT_SAMPLERATE,
		0, 0, nullptr,
		AudioCategory_GameMedia))) {
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
	if (FAILED(xaudio2->CreateSourceVoice(&source_voice, &wfx, XAUDIO2_VOICE_NOPITCH)))
	{
		ATLTRACE("err: create source voice failed\n");
		uninitialize_audio_engine();
		return -1;
	}

	last_frametime = 0.0;
	standard_frametime = xaudio2_play_frame_size * 1.0 / wfx.nSamplesPerSec * 1000; // in ms

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
	CEvent doneEvent(false, false, nullptr, nullptr);
	DWORD spinWaitResult;

	while (true) {
		DWORD dw = WaitForSingleObject(frame_ready_event, 1);
		if (dw != WAIT_OBJECT_0 && dw != WAIT_TIMEOUT) {
			ATLTRACE("err: wait frame ready event failed, code=%lu\n", GetLastError());
			InterlockedExchange(playback_state, audio_playback_state_stopped);
			break;
		} else if (dw == WAIT_TIMEOUT) {
			// check flag
			if (*playback_state == audio_playback_state_stopped ||
				*playback_state == audio_playback_state_init) {
				// pass
			}
			else if (file_stream_end) {
				ATLTRACE("info: decode stopped, fetch from fifo\n");
				SetEvent(frame_ready_event); // avoid deadlock
			}
			else 
				continue;
		}
		clock_t decode_begin_time = clock();

		EnterCriticalSection(audio_playback_section);
		
		int fifo_size = get_audio_fifo_cached_samples_size();
		if (fifo_size == 0 && decoder_is_running) {
			LeaveCriticalSection(audio_playback_section);
			Sleep(1);
			continue;
		}
		if (*playback_state == audio_playback_state_stopped) {
			// bypass
		}
		else if (!decoder_is_running && fifo_size == 0) {
			LeaveCriticalSection(audio_playback_section);
			// all done
			ATLTRACE("info: decoder stopped and fifo empty, ending playback thread\n");
			InterlockedExchange(playback_state, audio_playback_state_stopped);
			continue;
		}
		// if (fifo_size < xaudio2_play_frame_size) {
		// 	SetEvent(frame_underrun_event);
		// 	LeaveCriticalSection(audio_playback_section);
		//	Sleep(1);
		//	continue;
		// }
//			InterlockedExchange(playback_state, audio_playback_state_stopped);


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
			else if (fifo_size == 0 && state.BuffersQueued > 0)
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
				xaudio2_played_samples = 0;
				xaudio2_played_buffers = 0;
				is_playing = false;
				elapsed_time = 0.0;
				UINT32 raw = *reinterpret_cast<UINT32*>(&elapsed_time);
				WPARAM w = static_cast<WPARAM>(raw);
				AfxGetMainWnd()->PostMessage(WM_PLAYER_TIME_CHANGE, (WPARAM)raw);
				LeaveCriticalSection(audio_playback_section);
				// EnterCriticalSection(audio_playback_section);
				// bool need_clean = !user_request_stop;
				// LeaveCriticalSection(audio_playback_section);
				// if (need_clean)
				// 	reset_audio_context();
				break; // 读取结束
			}
		}

		// 创建输出缓冲区
		// get decoded frame from audio fifo

		//out_buffer_size = sizeof(uint8_t) * frame->nb_samples * wfx.nBlockAlign;
		// out_buffer_size = (
		// 	decode_lag_use_big_buffer 
		// 	? sizeof(uint8_t) * xaudio2_play_frame_size * wfx.nBlockAlign * static_cast<int>(ceil(last_frametime / standard_frametime))
		// 	: sizeof(uint8_t) * xaudio2_play_frame_size * wfx.nBlockAlign
		// );
		out_buffer_size = sizeof(uint8_t) * xaudio2_play_frame_size * wfx.nBlockAlign;
		if (out_buffer)
			delete[] out_buffer;
		out_buffer = DBG_NEW uint8_t[out_buffer_size];
		memset(out_buffer, 0, out_buffer_size);
		while (!TryEnterCriticalSection(audio_fifo_section)) {}
		// read_samples_from_fifo((uint8_t**)out_buffer, xaudio2_play_frame_size);
		uint8_t** fifo_buf = NULL;
		fifo_buf = (uint8_t**)av_calloc(decoder_audio_channels, sizeof(uint8_t*));
		int alloc_ret = 0;
		if ((alloc_ret = av_samples_alloc(fifo_buf, NULL, decoder_audio_channels, xaudio2_play_frame_size, decoder_audio_sample_fmt, 0)) < 0) {
			dialog_ffmpeg_critical_error(alloc_ret, __FILE__, __LINE__);
			CString debug_alloc;
			debug_alloc.Format(_T("info: avsampleformat = %d\nchannels = %d\nframe_size = %d"), decoder_audio_sample_fmt, decoder_audio_channels, xaudio2_play_frame_size);
			AfxMessageBox(debug_alloc, MB_ICONERROR | MB_OK);
			LeaveCriticalSection(audio_fifo_section);
			LeaveCriticalSection(audio_playback_section);
			InterlockedExchange(playback_state, audio_playback_state_stopped);
			break;
		}
		int ret = read_samples_from_fifo(fifo_buf, xaudio2_play_frame_size);
		if (ret < 0) {
			dialog_ffmpeg_critical_error(ret, __FILE__, __LINE__);
			av_freep(&fifo_buf[0]);
			av_free(fifo_buf);
			LeaveCriticalSection(audio_fifo_section);
			LeaveCriticalSection(audio_playback_section);
			InterlockedExchange(playback_state, audio_playback_state_stopped);
			break;
		}
		int out_samples = swr_convert(swr_ctx, &out_buffer, out_buffer_size,
			(const uint8_t**)fifo_buf, ret); // pass actual read samples
		av_freep(&fifo_buf[0]);
		av_free(fifo_buf);
		LeaveCriticalSection(audio_fifo_section);
		if (out_samples < 0) {
			dialog_ffmpeg_critical_error(out_samples, __FILE__, __LINE__);
			LeaveCriticalSection(audio_playback_section);
			break;
		}

		while (state.BuffersQueued >= 64)
		{
			spinWaitResult = WaitForSingleObject(doneEvent, 1);
			if (spinWaitResult == WAIT_TIMEOUT) {
				source_voice->GetState(&state);
			}
			if (*playback_state == audio_playback_state_init)
			{
				// if (state.BuffersQueued == 32)
				// {
				InterlockedExchange(playback_state, audio_playback_state_playing);
				source_voice->Start();
				AfxGetMainWnd()->PostMessage(WM_PLAYER_START);
				Sleep(5); // wait for consuming buffer
				// }
			}
		}

		// 将转换后的音频数据输出到xaudio2
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
		// if (*playback_state == audio_playback_state_init)
		// {
			// if (state.BuffersQueued == 32)
			// {
			//	InterlockedExchange(playback_state, audio_playback_state_playing);
			//	source_voice->Start();
			// 	AfxGetMainWnd()->PostMessage(WM_PLAYER_START);
			// }
		// }
		// else
		// {
			// fix: avoid crash
			auto samples_played_before = get_samples_played_per_session();
			auto samples_sum = xaudio2_played_samples;
			auto played_buffers = xaudio2_played_buffers; auto it = xaudio2_playing_buffers.begin();
			bool flag = false;
			while (it != xaudio2_playing_buffers.end())
			{
				XAUDIO2_BUFFER*& played_buffer = *it;
				played_buffers++;
				samples_sum += played_buffer->AudioBytes / wfx.nBlockAlign;
				if (samples_sum >= samples_played_before)
				{
					flag = true;
					break;
				}
				else
					++it;
			}

			if (it != xaudio2_playing_buffers.begin() && it != xaudio2_playing_buffers.end())
			{
				// --it;
				xaudio2_free_buffers.insert(xaudio2_free_buffers.end(),
					xaudio2_playing_buffers.begin(), it);
				xaudio2_playing_buffers.erase(xaudio2_playing_buffers.begin(), it);
				xaudio2_played_buffers = played_buffers - 1;
				xaudio2_played_samples = samples_sum - (*it)->AudioBytes / wfx.nBlockAlign;
				ATLTRACE("info: samples played=%lld, cur played_buffers=%lld, cur samples=%lld, xaudio2 buffer arr size=%lld\n",
					state.SamplesPlayed, played_buffers, samples_sum, xaudio2_playing_buffers.size());
				// std::printf("info: buffer played=%zd\n", played_buffers);
				elapsed_time = xaudio2_played_samples * 1.0 / wfx.nSamplesPerSec + this->pts_seconds;
				UINT32 raw = *reinterpret_cast<UINT32*>(&elapsed_time);
				WPARAM w = static_cast<WPARAM>(raw);
				AfxGetMainWnd()->PostMessage(WM_PLAYER_TIME_CHANGE, w);
			}
			else if (it == xaudio2_playing_buffers.end()) {
				// all played
				ATLTRACE("info: sum not feeding samples_played, %zu : %zu\n", samples_sum, samples_played_before);
				LeaveCriticalSection(audio_playback_section);
				SetEvent(frame_ready_event);
				continue;
			}

			clock_t decode_end_time = clock();
			double decode_time_ms = (decode_end_time - decode_begin_time) * 1000.0 / CLOCKS_PER_SEC;
			ATLTRACE("info: xaudio2 cpu time %lf ms , frame time %lf ms!\n",
				decode_time_ms, standard_frametime);
			if (decode_time_ms > standard_frametime) {
				ATLTRACE("warn: xaudio2 cpu time %lf ms exceeds frame time %lf ms!\n",
					decode_time_ms, standard_frametime);
				last_frametime = decode_time_ms;
			}
			// else
			// {
				// std::printf("info: buffer played=%zd\n", xaudio2_played_buffers);
			// }
			//  (wfx.wBitsPerSample / 8) * wfx.nChannels
		// }

		LeaveCriticalSection(audio_playback_section);
		EnterCriticalSection(audio_fifo_section);
		if (get_audio_fifo_cached_samples_size() < xaudio2_play_frame_size * 32) {
			// need more data
			ATLTRACE("info: audio fifo cached samples size=%d, frame underrun!\n", get_audio_fifo_cached_samples_size());
			SetEvent(frame_underrun_event);
		}
		else if (state.BuffersQueued < 32) {
			// enough data buffered
			SetEvent(frame_ready_event);
		}
		LeaveCriticalSection(audio_fifo_section);
	}
}

void MusicPlayer::audio_decode_worker_thread()
{
	while (true) {
		// frame underrun, notify decoder to decode more frames
		DWORD dw = WaitForSingleObject(frame_underrun_event, 1);
		if (dw != WAIT_OBJECT_0 && dw != WAIT_TIMEOUT) {
			ATLTRACE("err: wait for frame underrun event failed\n");
			break;
		}
		else if (dw == WAIT_TIMEOUT && decode_in_background && get_audio_fifo_cached_samples_size() < xaudio2_play_frame_size * 2048) {
			SetEvent(frame_underrun_event);
		}
		else if (dw == WAIT_TIMEOUT && !decode_in_background && get_audio_fifo_cached_samples_size() > 0) {
			SetEvent(frame_underrun_event);
		}
		else if (dw == WAIT_TIMEOUT && file_stream_end) {
			// pass
			SetEvent(frame_ready_event);
		}
		else if (dw == WAIT_OBJECT_0) {
			ResetEvent(frame_underrun_event);
		}
		else {
			continue;
		}
		clock_t decode_begin = clock();
		if (*playback_state == audio_playback_state_stopped) {
			ATLTRACE("info: playback stopped, decoder thread exiting\n");
			break;
		}
		if (file_stream_end) {
			ATLTRACE("info: file stream ended, decoder thread exiting\n");
			break;
		}

		if (*playback_state == audio_playback_state_init
			&& is_pause) {
			ATLTRACE("info: resume from pause, pts_seconds=%lf\n", pts_seconds);
			if (av_seek_frame(format_context, -1, pts_seconds * AV_TIME_BASE, AVSEEK_FLAG_ANY) < 0) {
				ATLTRACE("err: resume failed\n");
				InterlockedExchange(playback_state, audio_playback_state_stopped);
			}
			is_pause = false;
		}

		// 从输入文件中读取数据并解码
		if (av_read_frame(format_context, packet) < 0) {
			ATLTRACE("info: av_read_frame reached eof, decoder exiting\n");
			// InterlockedExchange(playback_state, audio_playback_state_stopped);
			break;
		}

		if (packet->stream_index != audio_stream_index) {
			SetEvent(frame_underrun_event);
			av_packet_unref(packet);
			continue; // skip non-audio packet
		}
		int ret = avcodec_send_packet(codec_context, packet);
		if (ret < 0) {
			dialog_ffmpeg_critical_error(ret, __FILE__, __LINE__);
			InterlockedExchange(playback_state, audio_playback_state_stopped);
			av_packet_unref(packet);
			break;
		}
		while (true) {
			int res = avcodec_receive_frame(codec_context, frame);
			if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
				break; // 没有更多帧
			}
			else if (res < 0) {
				dialog_ffmpeg_critical_error(res, __FILE__, __LINE__);
				InterlockedExchange(playback_state, audio_playback_state_stopped);
				break;
			}
			while (!TryEnterCriticalSection(audio_fifo_section)) {}
			int ret_code = 0;
			if ((ret_code = add_samples_to_fifo(frame->extended_data, frame->nb_samples)) < 0) {
				dialog_ffmpeg_critical_error(ret_code, __FILE__, __LINE__);
				InterlockedExchange(playback_state, audio_playback_state_stopped);
				LeaveCriticalSection(audio_fifo_section);
				break;
			}
			ATLTRACE("info: decoded frame nb_samples=%d, pts=%lld\n", frame->nb_samples, frame->pts);
			LeaveCriticalSection(audio_fifo_section);
			av_frame_unref(frame);
		}
		EnterCriticalSection(audio_fifo_section);
		if (decode_in_background && get_audio_fifo_cached_samples_size() >= xaudio2_play_frame_size * 2048) {
			// enough data buffered
			SetEvent(frame_ready_event);
		}
		else if (decode_in_background && get_audio_fifo_cached_samples_size() < xaudio2_play_frame_size * 2048) {
			// need more data
			SetEvent(frame_underrun_event);
		}
		else if (!decode_in_background && get_audio_fifo_cached_samples_size() > 0) {
			// enough data buffered
			SetEvent(frame_ready_event);
		}
		else {
			SetEvent(frame_underrun_event);
		}
		LeaveCriticalSection(audio_fifo_section);
		
		int player_bufferes_queued = (
			is_xaudio2_initialized()
			 ? decoder_query_xaudio2_buffer_size()
			 : 0
			);
		if (player_bufferes_queued < 4) {
			// buffer underrun, resume player thread to submit data immediately
			ATLTRACE("info: xaudio2 buffers queued=%d, notify player thread to submit data\n", player_bufferes_queued);
			SetEvent(frame_ready_event);
		}
		av_frame_unref(frame); // eof, err process -> proper unref
		av_packet_unref(packet);
		clock_t decode_end = clock();
		double decode_time_ms = (decode_end - decode_begin) * 1000.0 / CLOCKS_PER_SEC;
		ATLTRACE("info: decode cycle time=%lf ms\n", decode_time_ms);
	}
}



void MusicPlayer::init_decoder_thread() {
	audio_decoder_worker_thread = AfxBeginThread(
		[](LPVOID param) -> UINT {
			SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
			auto player = reinterpret_cast<MusicPlayer*>(param);
			AvSetMmThreadCharacteristics(_T("Pro Audio"), player->xaudio2_thread_task_index);
			player->decoder_is_running = true;
			player->audio_decode_worker_thread();
			player->decoder_is_running = false;
			return 0;
		},
		reinterpret_cast<LPVOID>(this),
		THREAD_PRIORITY_TIME_CRITICAL,
		0,
		CREATE_SUSPENDED,
		nullptr);
	ATLTRACE("info: decoder thread created, handle = %p\n", static_cast<void*>(audio_decoder_worker_thread));
	audio_decoder_worker_thread->m_bAutoDelete = false;
	SetEvent(frame_underrun_event);

	decode_in_background = true; file_stream_end = false;
	audio_playback_section = new CRITICAL_SECTION;
	InitializeCriticalSection(audio_playback_section);
	audio_fifo_section = new CRITICAL_SECTION;
	InitializeCriticalSection(audio_fifo_section);
	audio_decoder_worker_thread->ResumeThread();
}

inline void MusicPlayer::start_audio_playback()
{
	if (*playback_state == audio_playback_state_stopped) {
		reset_audio_context();
	}
	InterlockedExchange(playback_state, audio_playback_state_init);
	audio_player_worker_thread = AfxBeginThread(
		[](LPVOID param) -> UINT {
			SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
			auto player = reinterpret_cast<MusicPlayer*>(param);
			AvSetMmThreadCharacteristics(_T("Pro Audio"), player->xaudio2_thread_task_index);
			player->audio_playback_worker_thread();
			return 0;
		},
		reinterpret_cast<LPVOID>(this),
		THREAD_PRIORITY_TIME_CRITICAL,
		0,
		CREATE_SUSPENDED,
		nullptr);
	ATLTRACE("info: player thread created, handle = %p\n", static_cast<void*>(audio_player_worker_thread));
	audio_player_worker_thread->m_bAutoDelete = false;
	audio_player_worker_thread->ResumeThread();
	// notify decoder to start decoding
	is_playing = true; user_request_stop = false;
	decode_in_background = false;
}

void MusicPlayer::stop_audio_decode()
{
	if (audio_decoder_worker_thread
		&& audio_decoder_worker_thread->m_hThread != INVALID_HANDLE_VALUE)
	{
		InterlockedExchange(playback_state, audio_playback_state_stopped);
		SetEvent(frame_underrun_event);
		DWORD exitCode;
		if (::GetExitCodeThread(audio_decoder_worker_thread->m_hThread, &exitCode)) {
			if (exitCode == STILL_ACTIVE) {
				WaitForSingleObject(audio_decoder_worker_thread->m_hThread, INFINITE);
			}
		}
		delete audio_decoder_worker_thread;
		audio_decoder_worker_thread = nullptr;
	}
	release_audio_context();
}

void MusicPlayer::stop_audio_playback(int mode)
{
	// if decoder thread is running, stop decoder thread
	stop_audio_decode();
	if (audio_player_worker_thread
		&& audio_player_worker_thread->m_hThread != INVALID_HANDLE_VALUE) {
		while (!TryEnterCriticalSection(audio_playback_section));
		// EnterCriticalSection(audio_playback_section); <- this cause delay, spin wait instead
		user_request_stop = true;
		InterlockedExchange(playback_state, audio_playback_state_stopped);
		SetEvent(frame_ready_event);
		LeaveCriticalSection(audio_playback_section);

		source_voice->Stop(0);
		source_voice->FlushSourceBuffers();
		is_playing = false;
		uninitialize_audio_fifo();
		// wait for thread to terminate
		WaitForSingleObject(audio_player_worker_thread->m_hThread, INFINITE);
		// managed by mfc
		delete audio_player_worker_thread;
		audio_player_worker_thread = nullptr;
		DeleteCriticalSection(audio_playback_section);
		delete audio_playback_section;
		audio_playback_section = nullptr;
	}
	// terminated xaudio and ffmpeg, do cleanup
	xaudio2_free_buffer();
	xaudio2_destroy_buffer();
	xaudio2_played_samples = xaudio2_played_buffers = xaudio2_played_samples = xaudio2_played_buffers = 0;
	float pts_time_f = 0.0f;
	if (is_pause)
	{
		pts_time_f = static_cast<float>(pts_seconds);
	}
	else {
		elapsed_time = pts_time_f = 0.0f;
	}
	UINT32 raw = *reinterpret_cast<UINT32*>(&pts_time_f);
	WPARAM w = static_cast<WPARAM>(raw);
	AfxGetMainWnd()->PostMessage(WM_PLAYER_TIME_CHANGE, w);
	ResetEvent(frame_underrun_event);
	ResetEvent(frame_ready_event);
	if (mode == 0)
		reset_audio_context();
	else if (mode == -1)
		release_audio_context();
	is_playing = false;
}

int MusicPlayer::initialize_audio_fifo(AVSampleFormat sample_fmt, int channels, int nb_samples)
{
	audio_fifo = av_audio_fifo_alloc(sample_fmt, channels, nb_samples);
	if (!audio_fifo)
	{
		AfxMessageBox(_T("err: could not allocate audio fifo!"), MB_ICONERROR);
		return -1;
	}
	return 0;
}

int MusicPlayer::resize_audio_fifo(int nb_samples)
{
	int ret_value;
	if (!audio_fifo)
		return -1;
	if ((ret_value = av_audio_fifo_realloc(audio_fifo, nb_samples)) < 0) {
		dialog_ffmpeg_critical_error(ret_value, __FILE__, __LINE__);
		return ret_value;
	}
	return 0;
}

int MusicPlayer::add_samples_to_fifo(uint8_t** decoded_data, int nb_samples)
{
	if (!audio_fifo)
		return -1;
	int res = av_audio_fifo_write(audio_fifo, reinterpret_cast<void**>(decoded_data), nb_samples);
	if (res < 0) {
		// audio fifo will resize automatically
		dialog_ffmpeg_critical_error(res, __FILE__, __LINE__);
		return res;
	}
	// 	ATLTRACE("info: added %d samples to audio fifo\n", res);
	return 0;
}

int MusicPlayer::read_samples_from_fifo(uint8_t** output_buffer, int nb_samples)
{
	int ret;
	if (!audio_fifo)
		return -1;
	if ((ret = av_audio_fifo_read(audio_fifo, reinterpret_cast<void**>(output_buffer), nb_samples)) < 0) {
		dialog_ffmpeg_critical_error(ret, __FILE__, __LINE__);
		return -1;
	}
	return ret;
}

void MusicPlayer::drain_audio_fifo(int nb_samples)
{
	if (!audio_fifo)
		return;
	int ret;
	if ((ret = av_audio_fifo_drain(audio_fifo, nb_samples)) < 0) {
		dialog_ffmpeg_critical_error(ret, __FILE__, __LINE__);
	}
}

void MusicPlayer::reset_audio_fifo()
{
	if (!audio_fifo)
		return;
	av_audio_fifo_reset(audio_fifo);
}

int MusicPlayer::get_audio_fifo_cached_samples_size()
{
	if (!audio_fifo)
		return -1;
	return av_audio_fifo_size(audio_fifo);
}

void MusicPlayer::uninitialize_audio_fifo()
{
	if (audio_fifo)
	{
		av_audio_fifo_free(audio_fifo);
		audio_fifo = nullptr;
	}
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

int MusicPlayer::decoder_query_xaudio2_buffer_size()
{
	EnterCriticalSection(audio_playback_section);
	XAUDIO2_VOICE_STATE state;
	source_voice->GetState(&state);
	int buffer_size = state.BuffersQueued;
	LeaveCriticalSection(audio_playback_section);
	return buffer_size;
}

bool MusicPlayer::is_xaudio2_initialized()
{
	return swr_ctx && out_buffer && source_voice && mastering_voice && xaudio2;
}

size_t MusicPlayer::get_samples_played_per_session()
{
	XAUDIO2_VOICE_STATE state;
	source_voice->GetState(&state);
	return state.SamplesPlayed - base_offset;
}

void MusicPlayer::dialog_ffmpeg_critical_error(int err_code, const char* file, int line)
{
	char buf[1024] = { 0 };
	av_strerror(err_code, buf, 1024);
	CString message = _T("FFmpeg critical error: ");
	CString res{};
	res.Format(_T("%s (file: %s, line: %d)\n"), CString(buf).GetString(), CString(file).GetString(), line);
	message += res;
	message += stack_unwind();
	AfxMessageBox(message, MB_ICONERROR);
}

CString MusicPlayer::stack_unwind()
{
	const int maxFrames = 64;
	void* frames[maxFrames];
	USHORT captured = RtlCaptureStackBackTrace(0, maxFrames, frames, nullptr);

	HANDLE process = GetCurrentProcess();
	SymInitialize(process, NULL, TRUE);

	CString trace;
	for (USHORT i = 0; i < captured; ++i) {
		DWORD64 displacement = 0;
		char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)] = {0};
		PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = MAX_SYM_NAME;

		if (SymFromAddr(process, (DWORD64)frames[i], &displacement, symbol)) {
			trace.Append(CA2W(symbol->Name));

			IMAGEHLP_LINE64 line;
			DWORD dwDisplacement;
			memset(&line, 0, sizeof(line));
			line.SizeOfStruct = sizeof(line);

			if (SymGetLineFromAddr64(process, (DWORD64)frames[i], &dwDisplacement, &line)) {
				trace.AppendFormat(_T(" (%s: %d)"), CString(line.FileName).GetString(), line.LineNumber);
			}
			trace.Append(_T("\n"));
		}
	}

	SymCleanup(process);

	return trace;
}

MusicPlayer::MusicPlayer() :
	xaudio2_buffer_ended(DBG_NEW volatile unsigned long long),
	playback_state(DBG_NEW volatile unsigned long long),
	audio_position(DBG_NEW volatile unsigned long long),
	audio_playback_section(nullptr),
	frame_ready_event(CreateEvent(nullptr, FALSE, FALSE, nullptr)),
	frame_underrun_event(CreateEvent(nullptr, FALSE, FALSE, nullptr)),
	xaudio2_thread_task_index(new DWORD(0))
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

void MusicPlayer::SetMasterVolume(float volume)
{
	if (IsInitialized()) {
		if (volume < 0.0f) volume = 0.0f;
		if (volume > 1.0f) volume = 1.0f;
		mastering_voice->SetVolume(volume);
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
	if (is_playing) {
		user_request_stop = true;
		stop_audio_playback(-1);
	}
	stop_audio_decode();
	uninitialize_audio_engine();

	if (xaudio2_buffer_ended)	delete xaudio2_buffer_ended;
	if (playback_state)			delete playback_state;
	if (audio_position)			delete audio_position;
	if (audio_fifo) 			uninitialize_audio_fifo();

	if (file_stream)
	{
		file_stream->Close();
		delete file_stream;
		file_stream = nullptr;
	}
}
