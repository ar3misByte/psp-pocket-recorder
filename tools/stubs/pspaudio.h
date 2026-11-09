#pragma once
#define PSP_AUDIO_VOLUME_MAX 0x8000
#define PSP_AUDIO_NEXT_CHANNEL (-1)
#define PSP_AUDIO_FORMAT_STEREO 0
int sceAudioChReserve(int,int,int); int sceAudioChRelease(int); int sceAudioOutputBlocking(int,int,void*);
int sceAudioSRCChReserve(int,int,int); int sceAudioSRCChRelease(void); int sceAudioSRCOutputBlocking(int,void*);
