<div align="center">

<img src="res/MFCMusicPlayer.ico" width="80" style="vertical-align:middle;" alt="icon"> <br/>
<b>MFCMusicPlayer</b>

</div>

A Simple music player, written in C++. 
<!-- ![Build System](https://img.shields.io/badge/Build%20System-MSBuild-blueviolet?style=for-the-badge&logo=data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBzdGFuZGFsb25lPSJubyI/PjwhRE9DVFlQRSBzdmcgUFVCTElDICItLy9XM0MvL0RURCBTVkcgMS4xLy9FTiIgImh0dHA6Ly93d3cudzMub3JnL0dyYXBoaWNzL1NWRy8xLjEvRFREL3N2ZzExLmR0ZCI+PHN2ZyB0PSIxNzI4MTA5NjA3MzUyIiBjbGFzcz0iaWNvbiIgdmlld0JveD0iMCAwIDEwMjQgMTAyNCIgdmVyc2lvbj0iMS4xIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHAtaWQ9IjYxMjAiIHhtbG5zOnhsaW5rPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5L3hsaW5rIiB3aWR0aD0iMjQiIGhlaWdodD0iMjQiPjxwYXRoIGQ9Ik03MTguOTMzMzMzIDg1LjMzMzMzM0wzODcuODQgNDE2Ljg1MzMzM2wtMjA5LjA2NjY2Ny0xNjQuNjkzMzMzTDg3LjQ2NjY2NyAyOTguNjY2NjY3djQyNi42NjY2NjZsOTEuNzMzMzMzIDQ2LjUwNjY2NyAyMTAuMzQ2NjY3LTE2NC4yNjY2NjdMNzE5Ljc4NjY2NyA5MzguNjY2NjY3IDkzOC42NjY2NjcgODUwLjM0NjY2N1YxNzAuNjY2NjY3ek0xODYuNDUzMzMzIDYxMC4xMzMzMzNWNDExLjczMzMzM2wxMDQuMTA2NjY3IDEwMy42OHogbTUyNi4wOCA1NS4wNEw1MTQuMTMzMzMzIDUxMmwxOTguNC0xNTMuMTczMzMzeiIgcC1pZD0iNjEyMSIgZmlsbD0iI2ZmZmZmZiI+PC9wYXRoPjwvc3ZnPg==) -->

![FFmpeg](https://img.shields.io/badge/FFmpeg-latest-yellow?style=for-the-badge&logo=ffmpeg)
![CMake](https://img.shields.io/badge/Build%20System-CMake-064F8C?style=for-the-badge&logo=cmake)
![GitHub top language](https://img.shields.io/github/languages/top/lucas150670/MFCMusicPlayer?style=for-the-badge)
![GitHub commit activity](https://img.shields.io/github/commit-activity/w/lucas150670/MFCMusicPlayer?style=for-the-badge)
![GitHub License](https://img.shields.io/github/license/lucas150670/MFCMusicPlayer?style=for-the-badge)
![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/lucas150670/MFCMusicPlayer/build-release.yaml?style=for-the-badge)
[![State-of-the-art Shitcode](https://img.shields.io/static/v1?label=State-of-the-art&message=Shitcode&color=7B5804&style=for-the-badge)](https://github.com/trekhleb/state-of-the-art-shitcode)

***
This project will no longer be under maintenance. Farewell!
***

Features:
- CMake build system support
- decode & play music files with FFmpeg
- real-time playback with XAudio2, swresample -> 44.1kHz/16bit/2ch pcm output
- simple gui
- lyric display support
- word-by-word LRC parser / ESLyric parser, karaoke-like display
- translation / romanization support
- ARM64 build, tested on Snapdragon X Elite
- NCM music format support (algorithm from [音乐解锁](https://git.unlock-music.dev/um/web))
- Windows's SystemMediaTransportControls support
- audio spectrum display based on kissfft
- equalizer support based on libavfilter

Screenshot:

![Screenshot](res/screenshot-1.png)

Depends: FFmpeg, XAudio2, OpenSSL, rapidjson, cpp-base64, cppwinrt, kissfft

Supported Platform: Windows 10 version 10240 or later(in theory), Windows 11 latest(tested)

UI: MFC(main), Direct2D/DirectWrite(lyric), GDI+(spectrum)

License: MIT (main-program), LGPL (ffmpeg), MIT (Windows App SDK), [Microsoft DirectX Software Development Kit License](LICENSE.XAudio2.txt) (XAudio2, direct2d), MIT (NCM Decoder),
BSD-3-Clause(kissfft), Apache 2.0(openssl), [MIT](LICENSE.thirdparty.txt)(rapidjson), [cpp-base64 license](LICENSE.thirdparty.txt)(cpp-base64), MIT(cppwinrt)

Contact: lucas150670@petalmail.com


### note:
- no ffmpeg libraries (in either source code form, or binary form) are included in this repository.
- if you want to build this project yourself, you will need to download ffmpeg libraries yourself. please note that ffmpeg is distributed under different licenses, you should comply with the licenses of the libraries you download.
- automatic builds from [Github Actions](https://github.com/lucas150670/MFCMusicPlayer/actions) are available. ffmpeg libraries in build artifact are fetched from vcpkg directly and under LGPL license.
- resources used are under their own licenses. if their usage violated your right, please contact me.
- the project icon was created by [白菜叶_](https://space.bilibili.com/1954890407). this icon is not free for use, and is not distributed under the main program's license.  any utilization, reproduction, modification, or distribution of the icon is strictly prohibited without the explicit written authorization of the original author or lucas150670.
- MSBuild project files are only used to modify UI. Building the project with CMake is recommended, as MSBuild project is not tested and properly configured with 3rd party libraries.

### for mainland china users:
- you can access this project from [GitCode Mirror](https://gitcode.com/lucas150670/MFCMusicPlayer).

### credit (no particular order):
- [白菜叶_](https://space.bilibili.com/1954890407) (logo artist)
- [undefined](https://github.com/steve84966)
- [Zopiclone](https://github.com/Zopiclone-Main)
- Adam
- 任行道
- [Xx_Dark_D_xX](https://github.com/mizu-mio)
- [xuezhaju](https://github.com/xuezhaju)
- [Vladimir15963](https://github.com/Vladimir15963)
- [dachuan_DC](https://x.com/dachuan_DC)
- and anyone else who has contributed to this project / help test the program.

### sponsor the project
- [爱发电](https://afdian.com/a/lucas150670)

### ingredient of the project!

*そっと咲いて征く言葉だけ どうか綺麗で居て欲しいだけ*

*明日笑えるから今だけ 歌い続けるメイド*

*過去にバイバイバイ 今を愛愛愛してる 言えたら痛みにバイバイバイ*

*今日は魔法にかかったメイド ささやかな晴れ舞台*

<div align="right">-- MIMI; 重音テト - マジック・メイド</div>
