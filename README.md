# MFCMusicPlayer

A Simple music player, written in C++. 


Features: Updating.


Depends: FFmpeg, XAudio2


UI: MFC


License: MIT (main-program), LGPL (ffmpeg), MIT (Windows App SDK), [LICENSE.XAudio2.txt](LICENSE.XAudio2.txt) (XAudio2)


Contact: lucas150670@petalmail.com


ps: 

- no ffmpeg libraries (in either source code form, or binary form) are included in this repository.
- you need to get ffmpeg libraries by yourself. To compile this project, set ffmpeg include/lib path in Visual Studio -> Project Properties -> VC++ Directories -> Include Directories/Library Directories.
- if you choose to link against ffmpeg builds under GPL, the produced binary shall apply to GPL.
