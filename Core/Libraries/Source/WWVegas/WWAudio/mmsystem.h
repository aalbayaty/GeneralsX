/*
** mmsystem.h - Windows Multimedia System stub
**
** TheSuperHackers @build 15/12/2024
** Minimal stub for Linux builds. Download.cpp includes this on Windows only.
** For Linux builds with OpenAL, this provides empty type definitions.
*/

#pragma once

#if defined(_WIN32)
    // GeneralsX @build 22/07/2026 Native Windows builds (MinGW) must see the real
    // SDK header. This stub shadows it because WWAudio is on the include path;
    // #include_next resumes the search in the remaining (system) include dirs.
    #if defined(__GNUC__) || defined(__clang__)
        #include_next <mmsystem.h>
    #else
        // MSVC has no #include_next; it would also resolve to this stub since the
        // WWAudio include dir precedes the SDK. MSVC builds of this fork are not
        // currently exercised - fail loudly rather than silently define nothing.
        #error "WWAudio mmsystem.h stub cannot delegate to the Windows SDK header on this compiler"
    #endif
#elif defined(SAGE_USE_OPENAL)
    #ifndef _MMSYSTEM_H_
    #define _MMSYSTEM_H_

    // Empty stub - multimedia functions are not used in Linux builds
    // Download.cpp only uses this header preparation, actual multimedia
    // operations are stubbed or not called in Linux builds.

    #endif
#endif
