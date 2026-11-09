#pragma once
#define PSP_DISPLAY_PIXEL_FORMAT_8888 3
#define PSP_DISPLAY_SETBUF_NEXTFRAME 1
int sceDisplaySetMode(int,int,int); int sceDisplayWaitVblankStart(void); int sceDisplaySetFrameBuf(void*,int,int,int);
