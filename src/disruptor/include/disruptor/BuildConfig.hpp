#pragma once

#define DISRUPTOR_CPP_17
#define DISRUPTOR_CPP_11
#define DISRUPTOR_CPU_64

#define DISRUPTOR_STDCALL __attribute((stdcall))
#define DISRUPTOR_CDECL /* */
#define DISRUPTOR_FASTCALL __attribute((fastcall))
#define DISRUPTOR_ALIGN(x) __attribute(aligned(x))
#define DISRUPTOR_OS_FAMILY_LINUX

#ifdef _DEBUG
#define DISRUPTOR_BUILD_CONFIGURATION "Debug"
#else
#define DISRUPTOR_BUILD_CONFIGURATION "Release"
#endif
