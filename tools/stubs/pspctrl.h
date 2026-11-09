#pragma once
typedef struct { unsigned int TimeStamp, Buttons; unsigned char Lx, Ly, Rsrv[6]; } SceCtrlData;
enum { PSP_CTRL_SELECT=1, PSP_CTRL_START=8, PSP_CTRL_UP=16, PSP_CTRL_RIGHT=32, PSP_CTRL_DOWN=64, PSP_CTRL_LEFT=128, PSP_CTRL_LTRIGGER=256, PSP_CTRL_RTRIGGER=512, PSP_CTRL_TRIANGLE=4096, PSP_CTRL_CIRCLE=8192, PSP_CTRL_CROSS=16384, PSP_CTRL_SQUARE=32768 };
#define PSP_CTRL_MODE_DIGITAL 0
int sceCtrlSetSamplingCycle(int); int sceCtrlSetSamplingMode(int); int sceCtrlPeekBufferPositive(SceCtrlData*,int);
