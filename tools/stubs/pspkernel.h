#pragma once
#include <stdint.h>
typedef int SceUID; typedef unsigned int SceSize; typedef unsigned int SceUInt;
#define PSP_THREAD_ATTR_USER 0
int sceKernelCreateSema(const char*,int,int,int,void*); int sceKernelWaitSema(SceUID,int,SceUInt*);
int sceKernelSignalSema(SceUID,int); int sceKernelPollSema(SceUID,int);
SceUID sceKernelCreateThread(const char*, int(*)(SceSize,void*), int,int,int,void*);
int sceKernelStartThread(SceUID,SceSize,void*); int sceKernelDelayThread(SceUInt);
int sceKernelWaitThreadEnd(SceUID,SceUInt*); int sceKernelDeleteThread(SceUID);
typedef long long SceInt64;
#define THREAD_ATTR_USER 0
#define THREAD_ATTR_VFPU 0
#define PSP_MAIN_THREAD_ATTR(x)
#define PSP_HEAP_SIZE_KB(x)
int sceKernelCreateCallback(const char*, int(*)(int,int,void*), void*); int sceKernelRegisterExitCallback(int);
int sceKernelSleepThreadCB(void); SceInt64 sceKernelGetSystemTimeWide(void); void sceKernelExitGame(void);
