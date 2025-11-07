cmake_minimum_required(VERSION 3.24)
project(MFCMusicPlayer LANGUAGES CXX)

set(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS ON)
set(CMAKE_MFC_FLAG 2)

find_package(FFMPEG REQUIRED)

if ((${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang" AND "x${CMAKE_CXX_SIMULATE_ID}" STREQUAL "xMSVC")) # check if using clang-cl
    message("clang-cl detected!")
    add_compile_options(/EHa /EHs)
endif ()

add_executable(MFCMusicPlayer WIN32
    MFCMusicPlayer.cpp
    MFCMusicPlayerDlg.cpp
    MusicPlayer.cpp
    pch.cpp
    MFCMusicPlayer.rc
)

target_include_directories(MFCMusicPlayer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(MFCMusicPlayer
    PUBLIC
        cxx_std_20
)

target_precompile_headers(MFCMusicPlayer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/pch.h
)

target_compile_definitions(MFCMusicPlayer
    PUBLIC
        _WINDOWS
        UNICODE
        _UNICODE
        _AFXDLL
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
        $<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},X86>:WIN32>
)

target_link_libraries(MFCMusicPlayer
    PUBLIC
        $<$<STREQUAL:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID},x64>:Avrt.lib>
)

target_compile_options(MFCMusicPlayer
    PUBLIC
        /W3
)

target_link_options(MFCMusicPlayer
    PUBLIC
        /ENTRY:wWinMainCRTStartup
)

target_include_directories(MFCMusicPlayer PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(MFCMusicPlayer PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(MFCMusicPlayer PRIVATE ${FFMPEG_LIBRARIES})
