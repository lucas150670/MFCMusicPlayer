// pch.cpp: 与预编译标头对应的源文件

#include "pch.h"

// 当使用预编译的头时，需要使用此源文件，编译才能成功。



// link ffmpeg dll

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "swresample.lib")

// link xaudio2_9 dll

#pragma comment(lib, "xaudio2.lib")

// High DPI Patch
#pragma comment(lib, "Shcore.lib")

// DbgHelp
#pragma comment(lib, "dbghelp.lib")