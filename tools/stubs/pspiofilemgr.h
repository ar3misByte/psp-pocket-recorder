#pragma once
typedef struct { int st_mode; unsigned int st_attr; long long st_size; } SceIoStat;
typedef struct { SceIoStat d_stat; char d_name[256]; void *d_private; int dummy; } SceIoDirent;
#define PSP_O_RDONLY 1
#define PSP_O_WRONLY 2
#define PSP_O_CREAT 0x200
#define PSP_O_TRUNC 0x400
#define PSP_SEEK_SET 0
#define PSP_SEEK_END 2
int sceIoOpen(const char*,int,int); int sceIoClose(int); int sceIoWrite(int,const void*,int); int sceIoLseek32(int,int,int);
int sceIoMkdir(const char*,int); int sceIoRemove(const char*); int sceIoDopen(const char*); int sceIoDread(int,SceIoDirent*); int sceIoDclose(int);
int sceIoDevctl(const char*,unsigned int,void*,int,void*,int);
int sceAudioInputInit(int,int,int); int sceAudioInputBlocking(int,int,void*);
