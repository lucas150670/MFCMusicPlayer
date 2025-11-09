# MFCMusicPlayer

A Simple music player, written in C++. 

![Framework](https://img.shields.io/badge/Framework-MFC-blueviolet?style=plastic&logo=data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBzdGFuZGFsb25lPSJubyI/PjwhRE9DVFlQRSBzdmcgUFVCTElDICItLy9XM0MvL0RURCBTVkcgMS4xLy9FTiIgImh0dHA6Ly93d3cudzMub3JnL0dyYXBoaWNzL1NWRy8xLjEvRFREL3N2ZzExLmR0ZCI+PHN2ZyB0PSIxNzI4MTA5NjA3MzUyIiBjbGFzcz0iaWNvbiIgdmlld0JveD0iMCAwIDEwMjQgMTAyNCIgdmVyc2lvbj0iMS4xIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHAtaWQ9IjYxMjAiIHhtbG5zOnhsaW5rPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5L3hsaW5rIiB3aWR0aD0iMjQiIGhlaWdodD0iMjQiPjxwYXRoIGQ9Ik03MTguOTMzMzMzIDg1LjMzMzMzM0wzODcuODQgNDE2Ljg1MzMzM2wtMjA5LjA2NjY2Ny0xNjQuNjkzMzMzTDg3LjQ2NjY2NyAyOTguNjY2NjY3djQyNi42NjY2NjZsOTEuNzMzMzMzIDQ2LjUwNjY2NyAyMTAuMzQ2NjY3LTE2NC4yNjY2NjdMNzE5Ljc4NjY2NyA5MzguNjY2NjY3IDkzOC42NjY2NjcgODUwLjM0NjY2N1YxNzAuNjY2NjY3ek0xODYuNDUzMzMzIDYxMC4xMzMzMzNWNDExLjczMzMzM2wxMDQuMTA2NjY3IDEwMy42OHogbTUyNi4wOCA1NS4wNEw1MTQuMTMzMzMzIDUxMmwxOTguNC0xNTMuMTczMzMzeiIgcC1pZD0iNjEyMSIgZmlsbD0iI2ZmZmZmZiI+PC9wYXRoPjwvc3ZnPg==) ![GitHub top language](https://img.shields.io/github/languages/top/lucas150670/MFCMusicPlayer)
![GitHub commit activity](https://img.shields.io/github/commit-activity/w/lucas150670/MFCMusicPlayer)
![GitHub License](https://img.shields.io/github/license/lucas150670/MFCMusicPlayer)
![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/lucas150670/MFCMusicPlayer/build-release.yaml)
[![State-of-the-art Shitcode](https://img.shields.io/static/v1?label=State-of-the-art&message=Shitcode&color=7B5804)](https://github.com/trekhleb/state-of-the-art-shitcode)

Features: 

- decode & play music files with FFmpeg
- low latency playback with XAudio2, swresample -> 44.1kHz/16bit/2ch pcm output
- optimized for low-level processors (like intel twinlake, alderlake-n, jasper lake, etc.) using audio fifo
- simple gui
- playlist support (planned)
- spectrem with fft and direct2d (planned)
- bu xiang xie le


Depends: FFmpeg, XAudio2


UI: MFC


License: MIT (main-program), LGPL (ffmpeg), MIT (Windows App SDK), [LICENSE.XAudio2.txt](LICENSE.XAudio2.txt) (XAudio2)


Contact: lucas150670@petalmail.com


ps: 

- no ffmpeg libraries (in either source code form, or binary form) are included in this repository.
- you need to get ffmpeg libraries by yourself. To compile this project, set ffmpeg include/lib path in Visual Studio -> Project Properties -> VC++ Directories -> Include Directories/Library Directories.
- if you choose to link against ffmpeg builds under GPL, the produced binary shall apply to GPL.
- you can download GitHub automatic builds from [Github Actions](https://github.com/lucas150670/MFCMusicPlayer/actions). ffmpeg libraries in build artifact are fetched from vcpkg directly and under LGPL license.



You are the
![AccessCount](https://api.moedog.org/count/@MFCMusicPlayer.readme)th visitor of the README file!